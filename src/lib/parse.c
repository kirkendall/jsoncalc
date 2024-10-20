#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "json.h"

/* Here we need to access the "real" parse functions */
#ifdef JSON_DEBUG_MEMORY
# undef json_parse_string
#endif


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
		assert(scan->next == NULL);
		scan->next = more;
	} else {
		/* Next element, unoptimized */
		for (scan = container->first; scan->next; scan = scan->next) {
		}
		scan->next = more;
	}
	JSON_END_POINTER(container) = more;
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
		for (scan = container->first; strcmp(scan->text, more->text); scan = scan->next) {
			if (!scan->next) {
				/* adding a new name */
				scan->next = more;
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

/* Add data to an object, array, or key.  Returns 1 normally, or 0 for
 * errors (after calling json_throw()).
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

/* Parse an in-memory JSON document.  This could be a string, or an mmap()ed
 * file.
 */
static json_t *parse(const char *str, size_t len, const char **refend, const char **referr)
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

	/* Start with a stack containing an empty array.  We expect parsing to
	 * put one thing in the array.
	 */
	memset(&arraybuf, 0, sizeof arraybuf);
	arraybuf.type = JSON_ARRAY;
	sp = 0;
	stack[sp] = &arraybuf;

	/* Guess key size */
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
			error = "Unexpected character";
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
						     tail->next;
						     tail = tail->next) {
						}
					}
					tail->next = jk;
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
	/* Free up any partial results on the stack */
	while (sp > 0) {
		json_free(stack[sp]);
		sp--;
	}

	/* Stuff the error info into the appropriate places */
	if (refend)
		*refend = str;
	if (referr)
		*referr = error;
	return NULL;
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
		return json_error_null(1, "%s", error);
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
		return json_error_null(1, "%s", error);
	return result;
}
