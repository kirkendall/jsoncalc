#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <jsoncalc.h>

/* Here we need to access the "real" parse functions */
#ifdef JSON_DEBUG_MEMORY
# undef json_parse_string
#endif

/* This data type is used only in this file to track the list of registered
 * table formats.
 */
typedef struct jsonparser_s {
	struct jsonparser_s *other;
	const char	*name;
	int	(*tester)(const char *str, size_t len);
	json_t	*(*parser)(const char *str, size_t len, const char **refend, const char **referr);
} jsonparser_t;


/* This is used to store the details of a deferred array */
typedef struct {
	jsondef_t basic; /* normal stuff */
	char	*start;
	char	*end;
} jdefarray_t;

static json_t *jdefarray_first(json_t *defarray);
static json_t *jdefarray_next(json_t *defelem);
static json_t *jdefarray_islast(const json_t *defelem);

/* Append an element to an array */
static void jappendarray(json_t *container, json_t *more)
{
	json_t	*scan;

	if (!container->first) {
		/* First element */
		assert(JSON_END_POINTER(container) == NULL);
		container->first = more;
	} else if ((scan = JSON_END_POINTER(container)) != NULL) {
		/* Next element, optimized via JSON_POINTER_END() */
		assert(scan->next == NULL); /* undeferred */
		scan->next = more; /* undeferred */
	} else {
		/* Next element, unoptimized */
		for (scan = container->first; scan->next; scan = scan->next) { /* undeferred */
		}
		scan->next = more; /* undeferred */
	}
	JSON_END_POINTER(container) = more;
	JSON_ARRAY_LENGTH(container)++;
	if (container->text[1] == 't' && (more->type != JSON_OBJECT || more->first == NULL))
		container->text[1] = 'n';
}

/* Append a member to an object.  This version is only useable in the parser
 * because it assumes each member is new (no duplicates).
 */
static void jappendobject(json_t *container, json_t *more)
{
	json_t	*scan;

	if (!container->first) {
		container->first = more;
	} else {
		for (scan = container->first; strcmp(scan->text, more->text); scan = scan->next) { /* object */
			if (!scan->next) { /* object */
				/* adding a new name */
				scan->next = more; /* object */
				JSON_END_POINTER(container) = more;
				return;
			}
		}

		/* Replace the value of the member at "scan" */
		json_free(scan->first);
		scan->first = more->first;
		more->first = NULL;
		json_free(more);
	}
}

/* Add data to an object, array, or key.  Returns NULL normally, or an
 * error message if an error is detected.
 */
char *json_append(json_t *container, json_t *more)
{
	switch (container->type) {
	  case JSON_KEY:
		if (more->type == JSON_KEY)
			return "Attempt to add a key as a value of a key";

		/* If the key already has a value, free it before storing
		 * the new value.
		 */
		if (container->first)
			free(container->first);
		container->first = more;
		break;

	  case JSON_ARRAY:
		jappendarray(container, more);
		break;

	  case JSON_OBJECT:
		if (more->type != JSON_KEY)
			return "Attempt to add unkeyed data to an object";
		jappendobject(container, more);
		break;

	  case JSON_BADTOKEN:
		return "json_parse_append(..., JSON_BADTOKEN)";
		break;

	  default:
		return "Attempt to append into a non-container";
	}
	return NULL;
}

/* This scans an array's source to determine whether it is worth deferring.
 * Quickly moves past an array without storing it, and returns a pointer to
 * the first character after the array.  If refcount is non-NULL then store
 * the count of elements there.  If reftable is non-NULL then it stores a
 * flag indicating whether it is a table (non-empty array of objects).
 */
const char *jskim(const char *str, const char *end, int *refcount, int *reftable)
{
        int     nest = 1;	/* Nesting depth for [] and {} */
        int	count = 0;	/* number of elements */
        int	nonobject = 0;	/* boolean: any non-object elements? */

        /* Skip the '[' and trailing whitespace, to find the first element */
        do {
		str++;
	} while (isspace(*str));
	if (*str != ']') {
		count++;
		if (*str != '{')
			nonobject = 1;
	}

        /* Skip over data, counting array elements.  Initially, "str" should
         * point to the '[' at the start of an array.
         */
        for (; nest > 0 && str < end; str++) {
                switch (*str) {
                  case '"':
			/* Skip past the string, minding backslashes */
			str++;
			while (*str != '"')
				if (*str++ == '\\')
					str++;
			break;
		  case ',':
			/* If top-level, then count an element */
			if (nest == 1) {
				do {
					str++;
				} while (isspace(*str));
				if (*str != ']') {
					count++;
					if (*str != '{')
						nonobject = 1;
				}
				str--;
			}
			break;
                  case '[':
                  case '{':
			nest++;
			break;
                  case ']':
                  case '}':
			nest--;
			break;
                }
        }

	/* Return the results. */
	if (refcount)
		*refcount = count;
	if (reftable)
		*reftable = (count > 0 && !nonobject);
        return str;
}

/* Parse an in-memory JSON document.  This could be a string, or an mmap()ed
 * file.
 */
static json_t *parseJSON(const char *str, size_t len, const char **refend, const char **referr)
{
	json_t *stack[100];
	int	sp;
	json_t	arraybuf;
	json_t	*jc, *tail;
	const char	*end, *error;
	char	*key;
	size_t	keysize;
	size_t	tlen;	/* token length */
	int	escape;
	char	*emptyobject = "object";
	int	defersize = 0;

char *start = str;
	/* Get parser config */
	jc = json_by_key(json_config, "emptyobject");
	if (jc && jc->type == JSON_STRING)
		emptyobject = jc->text;
	jc = json_by_key(json_config, "defersize");
	if (jc && jc->type == JSON_NUMBER)
		defersize = json_int(jc);

	/* Start with a stack containing an empty array.  We expect parsing to
	 * put one thing in the array.
	 */
	memset(&arraybuf, 0, sizeof arraybuf);
	arraybuf.type = JSON_ARRAY;
	sp = 0;
	stack[sp] = &arraybuf;

	/* Guess key size.  If we encounter a longer member key after this,
	 * we'll reallocate it.
	 */
	keysize = 100;
	key = (char *)malloc(keysize);
	*key = '\0';

	/* Locate the end of the text */
	end = str + len;

	/* ... aaaaaand... begin! */
	jc = NULL;
	while (!stack[0]->first || sp > 0) {
		/* If we hit the end of the string without fully parsing
		 * anything, then that's an error
		 */
		if (str >= end) {
			error = "Incomplete JSON text";
			goto Error;
		}

		/* The next character determines the token type */
		switch (*str) {
		case '"':
			/* String or member key -- parse to unbackslashed ",
			 * noting whether any backslashes occur.
			 */
			str++;
			for (escape = 0, tlen = 0; str + tlen <= end && str[tlen] != '"'; tlen++) {
				if (str[tlen] == '\\') {
					escape = 1;
					tlen++;
				}
			}

			/* Is this supposed to be a key? Or a string value? */ 
			if (stack[sp]->type == JSON_OBJECT && !*key) {
				/* It's a key.  But it could still use escapes */
				if (escape) {
					/* Get the length when unescaped */
					size_t bytes = json_mbs_unescape(NULL, str, tlen);

					/* Enlarge buffer if necessary */
					if (bytes + 1 > keysize) {
						free(key);
						keysize = bytes + 20;
						key = (char *)malloc(keysize);
					}

					/* Decode escapes, copy key to keybuf */
					(void)json_mbs_unescape(key, str, tlen);
					key[bytes] = '\0';
				} else {
					/* Enlarge buffer if necessary */
					if (tlen + 1 > keysize) {
						free(key);
						keysize = tlen + 20;
						key = (char *)malloc(keysize);
					}

					/* Copy the key */
					strncpy(key, str, tlen);
					key[tlen] = '\0';
				}
			} else {
				/* It's a string value.  Set "jc" */
				/* If it has escapes, process them */
				if (escape) {
					/* Get the length when unescaped */
					size_t bytes = json_mbs_unescape(NULL, str, tlen);

					/* Allocate a big enough JSON_STRING */
					jc = json_string("", bytes);

					/* Copy the value into the string,
					 * converting any backslash escapes.
					 */
					(void)json_mbs_unescape(jc->text, str, tlen);
					jc->text[bytes] = '\0';
				} else {
					jc = json_string(str, tlen);
				}
			}

			/* Move "str" past the closing quote */
			str += tlen + 1;
			break;

		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '.':
			/* Number.  Collect digits */
			tlen = 0;
			if (str[tlen] == '-')
				tlen++;
			while (isdigit(str[tlen]))
				tlen++;
			if (str[tlen] == '.')
				tlen++;
			while (isdigit(str[tlen]))
				tlen++;
			if (str[tlen] == 'e' || str[tlen] == 'E') {
				tlen++;
				if (str[tlen] == '+' || str[tlen] == '-')
					tlen++;
				while (isdigit(str[tlen]))
					tlen++;
			}
			jc = json_number(str, tlen);
			str += tlen;
			break;

		case 't':
			/* "true" */
			if (strncmp(str, "true", 4) || isalnum(str[4]))
				goto BadSymbol;
			jc = json_bool(1);
			str += 4;
			break;

		case 'f':
			/* "false" */
			if (strncmp(str, "false", 5) || isalnum(str[5]))
				goto BadSymbol;
			jc = json_bool(0);
			str += 5;
			break;

		case 'n':
			/* "null" */
			if (strncmp(str, "null", 4) || isalnum(str[4]))
				goto BadSymbol;
			jc = json_null();
			str += 4;
			break;

		case '[':
			/* Start of an array  -- maybe deferred? */
			jc = json_array();
			if (defersize > 0 && (end - str) >= defersize) {
				/* Find the end of the array */
				int count, istable;
				const char *endarray = jskim(str, end, &count, &istable);
				/* Is it big enough to be worth deferring? */
				if ((endarray - str) >= defersize) {
					/* Yes, defer it */
					/*!!!*/
				}
			}

			str++;
			break;

		case ']':
			/* End of an array */
			if (stack[sp]->type != JSON_ARRAY) {
				error = "Missing }";
				goto Error;
			}
			sp--;
			str++;
			break;

		case '{':
			/* Start of object */
			jc = json_object();
			str++;
			break;

		case '}':
			/* End of object */
			if (stack[sp]->type != JSON_OBJECT) {
				error = "Missing ]";
				goto Error;
			}

			/* If empty object, maybe convert it to an empty
			 * string or array.
			 */
			if (!stack[sp]->first) {
				if (*emptyobject == 'a')
					stack[sp]->type = JSON_ARRAY;
				else if (*emptyobject == 's')
					stack[sp]->type = JSON_STRING;
			}
			sp--;
			str++;
			tail = NULL;
			break;

		case ':':
		case ',':
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			/* Whitespace can be ignored.  Surprisingly, so can
			 * colons and commas.
			 */
			while (isspace(*str) || *str == ':' || *str == ',')
				str++;
			break;

		default:
			/* Unexpected character */
#if 0
			error = "Unexpected character in JSON data";
#else
{static char buf[200]; sprintf(buf, "Unexpected character 0x%02x at offset %d, start=%.25s", *str & 0xff, (int)(str - start), start); error = buf; }
fprintf(stderr, "%.*s\n", len, start);
#endif
			goto Error;
		}

		/* If jc is set, add it to the container on the stack */
		if (jc) {

			if (stack[sp]->type == JSON_OBJECT) {
				json_t *jk;

				if (!*key) {
					error = "Object member has no key";
					json_free(jc);
					goto Error;
				}

				/* Combine the key and value */
				jk = json_key(key, jc);
				*key = '\0';

				/* We don't use json_append() here because
				 * we know this is a non-duplicate key and
				 * hence must append.  We want to skip scanning
				 * the whole object each time we do this, so
				 * we maintain a "tail" pointer.
				 */
				if (!stack[sp]->first)
					stack[sp]->first = jk;
				else {
					if (tail == NULL) {
						for (tail = stack[sp]->first;
						     tail->next; /* object */
						     tail = tail->next) { /* object */
						}
					}
					tail->next = jk; /* object */
				}
				tail = jk;
			} else {
				/* Append to an array.  Arrays maintain their
				 * own "tail" pointer so json_append() works
				 * efficiently here.
				 */
				json_append(stack[sp], jc);
			}

			/* If it's a new array or object, push it onto the stack
			 * so we can start to accumulate its members/elements
			 */
			if (jc->type == JSON_ARRAY || jc->type == JSON_OBJECT)
				stack[++sp] = jc;
		}

		jc = NULL;
	}

	/* Return the thing in the arraybuf */
	return arraybuf.first;

BadSymbol:
	error = "Bad symbol";

Error:
	/* Free up any partial results.  Since every item gets added to
	 * whatever object or array contains it immediately (even nested
	 * arrays and objects get added before they're fully parsed), we
	 * DON'T need to loop over the stack.  stack[0]->first contains
	 * &arraybuf, so it shouldn't be freed either, but arraybuf.first
	 * should.  And maybe jc, if it isn't NULL.
	 */
	json_free(arraybuf.first);
	if (jc)
		json_free(jc);

	/* Stuff the error info into the appropriate places */
	if (refend)
		*refend = str;
	if (referr)
		*referr = error;
	return NULL;
}

/* List of known parsers other than JSON */
jsonparser_t *parsers;

static json_t *parse(const char *str, size_t len, const char **refend, const char **referr)
{
	jsonparser_t *jp;

	/* If any add-on parser wants it, let it parse try */
	for (jp = parsers; jp; jp = jp->other) {
		if (jp->tester(str, len))
			return jp->parser(str, len, refend, referr);
	}

	/* Otherwise, fall back on the JSON parser */
	return parseJSON(str, len, refend, referr);
}


/* Parse a string and return its json_t.  If there's an error, then it will
 * return a "null" json_t containing the error text.
 */
json_t *json_parse_string(const char *str)
{
	const char 	*end, *error;
	json_t	*result;

	/* Parse it */
	result = parse(str, strlen(str), &end, &error);

	/* If error, then return a "null" json_t with an error message */
	if (!result)
		return json_error_null(NULL, "%s", error);
	return result;
}

/* Parse a file and return its json_t.  Returns NULL if the file can't be
 * opened.  If it can be opened but not parsed, it returns a "null" json_t
 * containing the error message.  Otherwise it returns the parsed data.
 */
json_t *json_parse_file(const char *filename)
{
	jsonfile_t *jf;
	const char	*end, *error;
	json_t	*result;

	/* Map the file into memory */
	jf = json_file_load(filename);
	if (!jf)
		return NULL;

	/* Parse it */
	result = parse(jf->base, jf->size, &end, &error);

	/* Close/unmap the file */
	json_file_unload(jf);

	/* If error, then return a "null" json_t with an error message */
	if (!result)
		return json_error_null(NULL, "%s", error);
	return result;
}


/* Register a new type of parser.  The arguments are the parser's name, a
 * pointer to a tester function, and a pointer to a parser function.  If the
 * tester function returns a non-zero value, then the parser function is used
 * to parse this data.
 */
void json_parse_hook(const char *name, int (*tester)(const char *str, size_t len), json_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr))
{
	jsonparser_t *jp;

	/* Allocate a new parser, and initialize it */
	jp = (jsonparser_t *)malloc(sizeof *jp);
	jp->name = name;
	jp->tester = tester;
	jp->parser = parser;

	/* Add it to the list */
	jp->other = parsers;
	parsers = jp;
}
