#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <jx.h>

/* Here we need to access the "real" parse functions */
#ifdef JX_DEBUG_MEMORY
# undef jx_parse_string
#endif

/* This data type is used only in this file to track the list of registered
 * table formats.
 */
typedef struct jxparser_s {
	struct jxparser_s *other;
	const char	*name;
	int	(*tester)(const char *str, size_t len);
	jx_t	*(*parser)(const char *str, size_t len, const char **refend, const char **referr);
	int	(*updater)(jx_t *data, const char *filename);
} jxparser_t;

static jx_t *parseJSON(const char *str, size_t len, const char **refend, const char **referr, int allowdefer);

/******************************************************************************/
/* This next section of code is all in support of deferred arrays.            */

/* This is used to store the details of a deferred array */
typedef struct {
	jxdef_t basic; /* normal stuff */
	const char *start;/* position within that file where array starts */
	const char *end;  /* where it ends */
} jdefarray_t;

/* Parse the first element of the array, and return it.  This also involves
 * making a copy of the array's JX_DEFER node and related data.
 */
static jx_t *jdefarray_first(jx_t *array)
{
	/* Parse the first element.  Deferred arrays always have at least one
	 * element.
	 */
	jdefarray_t *def = (jdefarray_t *)array->first;
	jdefarray_t *nextdef;
	const char *next;
	jx_t *elem = parseJSON(def->start, (def->end - def->start), &next, NULL, 0);

	/* Make its "->next" point to a copy of "def" with its "->start"
	 * pointing to the next element's position in the data source code.
	 * Note that we don't copy basic.file because files' ref counts are
	 * maintained per deferred array, not per deferred element.
	 */
	elem->next = jx_defer(def->basic.fns);
	nextdef = (jdefarray_t *)elem->next;
	nextdef->basic.fns = def->basic.fns;
	nextdef->start = next;
	nextdef->end = def->end;

	/* Return the element. */
	return elem;
}

/* Parse the next element of the array and return it.  This also frees the
 * previous element but reuses its JX_DEFER node.  If there is no next
 * element then also free the JX_DEFER node and return NULL.
 */
static jx_t *jdefarray_next(jx_t *elem)
{
	jdefarray_t *def = (jdefarray_t *)elem->next;
	const char *next;

	/* Parse the next element.  If none, then return NULL and trust the
	 * jx_next() function (which calls this) to do the cleanup.
	 */
	jx_t *nextelem = parseJSON(def->start, (def->end - def->start), &next, NULL, 0);
	if (!nextelem)
		return NULL;

	/* Reuse the "def" with the next element, tweaking its "start" to point
	 * to the next next element.
	 */
	nextelem->next = (jx_t *)def;
	def->start = next;

	/* Free the previous element, but not its ->next */
	elem->next = NULL;
	jx_free(elem);

	return nextelem;
}

/* Test whether the current element is the last element. */
static int jdefarray_islast(const jx_t *elem)
{
	jdefarray_t *def = (jdefarray_t *)elem->first;
	const char *skip;

	/* "start" points to the next element's source code. Skip over
	 * whitespace and commas, and then check whether we hit a "]".
	 *
	 */
	for (skip = def->start; *skip == ',' || isspace(*skip); skip++) {
	}
	return *skip == ']';
}

static jxdeffns_t jdefarrayfns = {
	sizeof(jdefarray_t),	/* size */
	"JSON",			/* desc */
	jdefarray_first,	/* first */
	jdefarray_next,		/* next */
	jdefarray_islast,	/* islast */
	NULL,			/* free */
	NULL,			/* byindex */
	NULL			/* bykey */
};

/******************************************************************************/


/* Append an element to an array */
static void jappendarray(jx_t *container, jx_t *more)
{
	jx_t	*scan;

	if (!container->first) {
		/* First element */
		assert(JX_END_POINTER(container) == NULL);
		container->first = more;
	} else if ((scan = JX_END_POINTER(container)) != NULL) {
		/* Next element, optimized via JX_POINTER_END() */
		assert(scan->next == NULL); /* undeferred */
		scan->next = more; /* undeferred */
	} else {
		/* Next element, unoptimized */
		for (scan = container->first; scan->next; scan = scan->next) { /* undeferred */
		}
		scan->next = more; /* undeferred */
	}
	JX_END_POINTER(container) = more;
	JX_ARRAY_LENGTH(container)++;
	if (container->text[1] == 't' && (more->type != JX_OBJECT || more->first == NULL))
		container->text[1] = 'n';
}

/* Append a member to an object.  This version is only useable in the parser
 * because it assumes each member is new (no duplicates).
 */
static void jappendobject(jx_t *container, jx_t *more)
{
	jx_t	*scan;

	if (!container->first) {
		container->first = more;
	} else {
		for (scan = container->first; strcmp(scan->text, more->text); scan = scan->next) { /* object */
			if (!scan->next) { /* object */
				/* adding a new name */
				scan->next = more; /* object */
				JX_END_POINTER(container) = more;
				return;
			}
		}

		/* Replace the value of the member at "scan" */
		jx_free(scan->first);
		scan->first = more->first;
		more->first = NULL;
		jx_free(more);
	}
}

/* Add data to an object, array, or key.  Returns NULL normally, or an
 * error message if an error is detected.
 */
char *jx_append(jx_t *container, jx_t *more)
{
	assert(container != NULL && more != NULL);
	assert(container->type == JX_ARRAY || container->type == JX_OBJECT || container->type == JX_KEY);
	assert(container->type != JX_OBJECT || more->type == JX_KEY);

	switch (container->type) {
	  case JX_KEY:
		if (more->type == JX_KEY)
			return "Attempt to add a key as a value of a key";

		/* If the key already has a value, free it before storing
		 * the new value.
		 */
		if (container->first)
			free(container->first);
		container->first = more;
		break;

	  case JX_ARRAY:
		jappendarray(container, more);
		break;

	  case JX_OBJECT:
		if (more->type != JX_KEY)
			return "Attempt to add unkeyed data to an object";
		jappendobject(container, more);
		break;

	  case JX_BADTOKEN:
		return "jx_parse_append(..., JX_BADTOKEN)";
		break;

	  default:
		return "Attempt to append into a non-container";
	}
	return NULL;
}

/* This scans an array's source to determine whether it is worth deferring.
 * It quickly moves past an array without storing it, and returns a pointer to
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
static jx_t *parseJSON(const char *str, size_t len, const char **refend, const char **referr, int allowdefer)
{
	jx_t *stack[100];
	int	sp;
	jx_t	arraybuf;
	jx_t	*jc, *tail;
	const char	*end, *error;
	char	*key;
	size_t	keysize;
	size_t	tlen;	/* token length */
	int	escape;
	char	*emptyobject = "object";
	int	defersize = 0;

	/* Get parser config */
	jc = jx_by_key(jx_config, "emptyobject");
	if (jc && jc->type == JX_STRING)
		emptyobject = jc->text;
	jc = jx_by_key(jx_config, "defersize");
	if (jc && jc->type == JX_NUMBER)
		defersize = jx_int(jc);

	/* Start with a stack containing an empty array.  We expect parsing to
	 * put one thing in the array.
	 */
	memset(&arraybuf, 0, sizeof arraybuf);
	arraybuf.type = JX_ARRAY;
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
			if (stack[sp]->type == JX_OBJECT && !*key) {
				/* It's a key.  But it could still use escapes */
				if (escape) {
					/* Get the length when unescaped */
					size_t bytes = jx_mbs_unescape(NULL, str, tlen);

					/* Enlarge buffer if necessary */
					if (bytes + 1 > keysize) {
						free(key);
						keysize = bytes + 20;
						key = (char *)malloc(keysize);
					}

					/* Decode escapes, copy key to keybuf */
					(void)jx_mbs_unescape(key, str, tlen);
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
					size_t bytes = jx_mbs_unescape(NULL, str, tlen);

					/* Allocate a big enough JX_STRING */
					jc = jx_string("", bytes);

					/* Copy the value into the string,
					 * converting any backslash escapes.
					 */
					(void)jx_mbs_unescape(jc->text, str, tlen);
					jc->text[bytes] = '\0';
				} else {
					jc = jx_string(str, tlen);
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
			jc = jx_number(str, tlen);
			str += tlen;
			break;

		case 't':
			/* "true" */
			if (strncmp(str, "true", 4) || isalnum(str[4]))
				goto BadSymbol;
			jc = jx_boolean(1);
			str += 4;
			break;

		case 'f':
			/* "false" */
			if (strncmp(str, "false", 5) || isalnum(str[5]))
				goto BadSymbol;
			jc = jx_boolean(0);
			str += 5;
			break;

		case 'n':
			/* "null" */
			if (strncmp(str, "null", 4) || isalnum(str[4]))
				goto BadSymbol;
			jc = jx_null();
			str += 4;
			break;

		case '[':
			/* Start of an array  -- maybe deferred? */
			jc = jx_array();
			if (allowdefer && defersize > 0 && (end - str) >= defersize) {
				/* Find the end of the array */
				int count, istable;
				const char *endarray = jskim(str, end, &count, &istable);
				/* Is it big enough to be worth deferring? */
				if ((endarray - str) >= defersize) {
					/* Yes, defer it */
					jdefarray_t *def;
					jc->text[1] = istable ? 't' : 'n';
					JX_ARRAY_LENGTH(jc) = count;
					jc->first = jx_defer(&jdefarrayfns);
					def = (jdefarray_t *)jc->first;
					def->start = str + 1;
					def->end = endarray;
					def->basic.file = jx_file_containing(str, NULL);
					if (def->basic.file)
						def->basic.file->refs++;

					/* Move past the array.  "- 1" because
					 * of the "str++" before "break".
					 */
					str = endarray - 1;
				}
			}
			str++;
			break;

		case ']':
			/* End of an array */
			if (stack[sp]->type != JX_ARRAY) {
				error = "Missing }";
				goto Error;
			}
			sp--;
			str++;
			break;

		case '{':
			/* Start of object */
			jc = jx_object();
			str++;
			break;

		case '}':
			/* End of object */
			if (stack[sp]->type != JX_OBJECT) {
				error = "Missing ]";
				goto Error;
			}

			/* If empty object, maybe convert it to an empty
			 * string or array.
			 */
			if (!stack[sp]->first) {
				if (*emptyobject == 'a')
					stack[sp]->type = JX_ARRAY;
				else if (*emptyobject == 's')
					stack[sp]->type = JX_STRING;
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
			error = "Unexpected character in JSON data";
			goto Error;
		}

		/* If jc is set, add it to the container on the stack */
		if (jc) {

			if (stack[sp]->type == JX_OBJECT) {
				jx_t *jk;

				if (!*key) {
					error = "Object member has no key";
					jx_free(jc);
					goto Error;
				}

				/* Combine the key and value */
				jk = jx_key(key, jc);
				*key = '\0';

				/* We don't use jx_append() here because
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
				 * own "tail" pointer so jx_append() works
				 * efficiently here.
				 */
				jx_append(stack[sp], jc);
			}

			/* If it's a new array or object, push it onto the stack
			 * so we can start to accumulate its members/elements.
			 * Except if deferred array.
			 */
			if ((jc->type == JX_ARRAY && !jx_is_deferred_array(jc)) || jc->type == JX_OBJECT)
				stack[++sp] = jc;
		}

		jc = NULL;
	}

	/* Return the thing in the arraybuf */
	if (refend)
		*refend = str;
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
	jx_free(arraybuf.first);
	if (jc)
		jx_free(jc);

	/* Stuff the error info into the appropriate places */
	if (refend)
		*refend = str;
	if (referr)
		*referr = error;
	return NULL;
}

/* List of registered parsers (other than the built-in JSON parser) */
jxparser_t *parsers;

/* This is used by both jx_parse_file() and jx_parse_string() to do the
 * actual JSON parsing.
 */
static jx_t *parse(const char *str, size_t len, const char **refend, const char **referr, int allowdefer)
{
	jxparser_t *jp;

	/* If any add-on parser wants it, let it parse try */
	for (jp = parsers; jp; jp = jp->other) {
		if (jp->tester(str, len))
			return jp->parser(str, len, refend, referr);
	}

	/* How about binary? */
	if (jx_blob_test(str, len))
		return jx_blob_parse(str, len, refend, referr);

	/* Otherwise, fall back on the JSON parser */
	return parseJSON(str, len, refend, referr, allowdefer);
}


/* Parse a string and return its jx_t.  If there's an error, then it will
 * return a "null" jx_t containing the error text.
 */
jx_t *jx_parse_string(const char *str)
{
	const char 	*end, *error;
	jx_t	*result;

	/* Parse it */
	result = parse(str, strlen(str), &end, &error, 0);

	/* If error, then return a "null" jx_t with an error message */
	if (!result)
		return jx_error_null(NULL, "%s", error);
	return result;
}

/* Parse a file and return its jx_t.  Returns NULL if the file can't be
 * opened.  If it can be opened but not parsed, it returns a "null" jx_t
 * containing the error message.  Otherwise it returns the parsed data.
 */
jx_t *jx_parse_file(const char *filename)
{
	jxfile_t *jf;
	const char	*end, *error;
	jx_t	*result;

	/* Map the file into memory */
	jf = jx_file_load(filename);
	if (!jf)
		return NULL;

	/* Parse it */
	result = parse(jf->base, jf->size, &end, &error, 1);

	/* Close/unmap the file */
	jx_file_unload(jf);

	/* If error, then return a "null" jx_t with an error message */
	if (!result)
		return jx_error_null(NULL, "%s", error);
	return result;
}


/* Register a new type of parser.  The arguments are the parser's name, a
 * pointer to a tester function, and a pointer to a parser function.  If the
 * tester function returns a non-zero value, then the parser function is used
 * to parse this data.
 */
void jx_parse_hook(
	const char *plugin,
	const char *name,
	const char *suffix,
	const char *mimetype,
	int (*tester)(const char *str, size_t len),
	jx_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr),
	int (*updater)(jx_t *data, const char *filename))
{
	jx_t	*table, *row;
	jxparser_t	*jp, *scan;

	/* Allocate a new jxparser_t for it */
	jp = (jxparser_t *)malloc(sizeof *jp);
	jp->other = NULL;
	jp->name = name;
	jp->tester = tester;
	jp->parser = parser;
	jp->updater = updater;

	/* Add it to the end of the list */
	if (parsers) {
		for (scan = parsers; scan->other; scan = scan->other) {
		}
		scan->other = jp;
	} else {
		parsers = jp;
	}

	/* Add a row to the "parsers" table in jx_system */
	table = jx_by_key(jx_system, "parsers");
	row = jx_object();
	jx_append(row, jx_key("name", jx_string(name, -1)));
	jx_append(row, jx_key("plugin", plugin ? jx_string(plugin, -1) : jx_null()));
	jx_append(row, jx_key("suffix", suffix ? jx_string(suffix, -1) : jx_null()));
	jx_append(row, jx_key("mimetype", mimetype ? jx_string(mimetype, -1) : jx_null()));
	jx_append(row, jx_key("writable", jx_boolean(updater != NULL)));
	jx_append(table, row);
}
