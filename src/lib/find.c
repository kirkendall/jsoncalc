/* find.c */
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include "json.h"

/* This stores the search criteria and a incremental results */
typedef struct {
	json_t	*needle;	/* String or number to search for */
	regex_t *regex;		/* Regular expression to search for */
	char	*needkey;	/* If not NULL, key must match this */
	int	needint;	/* Integer to search for */
	double	needdouble;	/* Double to search for */
	int	int_or_double;	/* 'i' for needing, else needdouble */
	int	ignorecase;	/* non-zero to let uppercase match lowercase */
	int	index;		/* Outermost array subscript of match */
	char	*key;		/* Innermost object member key of match */
	char	*expr;		/* Buffer for building an expression ex match */
	size_t	size;		/* Size of expr buffer */
	size_t	used;		/* Amount of expr buffer that's used now */
	json_t	*result;	/* Table of found matches */
} jsonfind_t;

/* Append a string to find->expr, expanding it if necessary */
static void help_find_cat(jsonfind_t *find, char *str)
{
	size_t len = strlen(str);
	size_t newsize = find->used + len + 1;
	if (find->used > 0 && *str != '[')
		newsize++; /* because we'll need to add a "." */
	if (newsize > find->size) {
		/* Round up to a multiple of 32 bytes */
		newsize = ((newsize - 1) | 0x1f) + 1;

		/* Reallocate the buffer */
		find->expr = realloc(find->expr, newsize);
		find->size = newsize;
	}

	/* Append it */
	if (find->used > 0 && *str != '[')
		find->expr[find->used++] = '.';
	strcpy(find->expr + find->used, str);
	find->used += len;
}

/* Do a deep search for a value.  This is a helper function for jfn_find(),
 * which implement's JsonCalc's find() function.
 */
static void help_find(json_t *haystack, jsonfind_t *find)
{
	json_t	*scan, *found;
	int	wasused, wasindex, i;
	char	*waskey;
	char	indexstr[40];

	/* Arrays and objects are treated a bit differently */
	if (haystack->type == JSON_ARRAY) {
		/* For each element... */
		for (i = 0, scan = haystack->first; scan; i++, scan = scan->next) {
			/* If the value is an object or array, recurse */
			if (scan->type == JSON_OBJECT || scan->type == JSON_ARRAY) {
				/* Append this element's index to expr */
				wasused = find->used;
				snprintf(indexstr, sizeof indexstr, "[%d]", i);
				help_find_cat(find, indexstr);
				wasindex = find->index;
				if (find->index == -1)
					find->index = i;

				/* Recurse */
				help_find(scan, find);

				/* Restore expr */
				find->used = wasused;
				find->index = wasindex;
				continue;
			} else if (find->needkey && (!find->key || 0 != json_mbs_casecmp(find->needkey, find->key) )) {
				/* Wrong key.  The only reason we're scanning
				 * this array is that it might have an element
				 * that's an object with needkey, but this
				 * element isn't and object/array.
				 */
				continue;
			} else if (scan->type == JSON_STRING && find->needle->type == JSON_STRING) {
				/* Compare as strings */
				if (find->ignorecase) {
					if (0 != json_mbs_casecmp(find->needle->text, scan->text))
						continue;
				} else {
					if (0 != strcmp(find->needle->text, scan->text))
						continue;
				}
			} else if (scan->type == JSON_STRING && find->regex) {
				/* Compare against the regexp */
				regmatch_t matches[10];
				if (regexec(find->regex, scan->text, 10, matches, 0) != 0)
					continue;
			} else if (scan->type == JSON_NUMBER) {
				/* Does it match? */

				/* Optimization for comparing binary integers */
				if (scan->text[0] == 0 && scan->text[1] == 'i' && find->int_or_double == 'i') {
					/* Compare binary integers */
					if (json_int(scan->first) != find->needint)
						continue;
				} else {
					/* Compare as double */
					if (json_double(scan->first) != find->needdouble)
						continue;
				}

			} else {
				/* it can't be what we're looking for */
				continue;
			}

			/* If we get here, it matched.  We need to append this
			 * element's index to expr, and then add a match to the
			 * result array
			 */
			wasused = find->used;
			wasindex = find->index;
			snprintf(indexstr, sizeof indexstr, "[%d]", i);
			help_find_cat(find, indexstr);
			if (find->index == -1)
				find->index = i;
			found = json_object();
			json_append(found, json_key("index", json_from_int(find->index)));
			if (find->key)
				json_append(found, json_key("key", json_string(find->key, -1)));
			json_append(found, json_key("value", json_copy(scan)));
			json_append(found, json_key("expr", json_string(find->expr, find->used)));
			json_append(find->result, found);

			/* Restore expr */
			find->used = wasused;
			continue;
		}
	} else /* JSON_OBJECT */ {
		/* For each member... */
		for (scan = haystack->first; scan; scan = scan->next) {
			/* If the value is an object or array, recurse */
			if (scan->first->type == JSON_OBJECT || scan->first->type == JSON_ARRAY) {
				/* Append this member's key to expr */
				wasused = find->used;
				help_find_cat(find, scan->text);

				/* Store the key so that if we're scanning an
				 * array, we'll know which array this is.
				 */
				waskey = find->key;
				find->key = scan->text;

				/* Recurse */
				help_find(scan->first, find);

				/* Restore expr */
				find->key = waskey;
				find->used = wasused;
				continue;
			} else if (find->needkey && 0 != json_mbs_casecmp(find->needkey, scan->text)) {
				/* Wrong key */
				continue;
			} else if (scan->first->type == JSON_STRING && find->needle && find->needle->type == JSON_STRING) {
				/* Compare as strings */
				if (find->ignorecase) {
					if (0 != json_mbs_casecmp(find->needle->text, scan->first->text))
						continue;
				} else {
					if (0 != strcmp(find->needle->text, scan->first->text))
						continue;
				}
			} else if (scan->first->type == JSON_STRING && find->regex) {
				/* Compare against the regexp */
				regmatch_t matches[10];
				if (regexec(find->regex, scan->first->text, 10, matches, 0) != 0)
					continue;
			} else if (scan->first->type == JSON_NUMBER) {
				/* Does it match? */

				/* Optimization for comparing binary integers */
				if (scan->first->text[0] == 0 && scan->first->text[1] == 'i' && find->int_or_double == 'i') {
					/* Compare binary integers */
					if (json_int(scan->first) != find->needint)
						continue;
				} else {
					/* Compare as double */
					if (json_double(scan->first) != find->needdouble)
						continue;
				}

			} else {
				/* it can't be what we're looking for */
				continue;
			}

			/* If we get here, it matched.  We need to append this
			 * member's key to expr, and then add a match to the
			 * result array
			 */
			wasused = find->used;
			help_find_cat(find, scan->text);
			found = json_object();
			if (find->index >= 0)
				json_append(found, json_key("index", json_from_int(find->index)));
			json_append(found, json_key("key", json_string(scan->text, -1)));
			json_append(found, json_key("value", json_copy(scan->first)));
			json_append(found, json_key("expr", json_string(find->expr, find->used)));
			json_append(find->result, found);

			/* Restore expr */
			find->used = wasused;
			continue;
		}
	}
}

/* Search for a given value.  This is a deep search, meaning it'll look through
 * any nested objects or arrays too.
 * 
 * "haystack" is an object or array to search through, "needle" is the string
 * or number to search for, "ignorecase" makes string searches be
 * case-insensitive, "regex" if not null will override "needle" and search
 * for a string via a regular expression, and "key" if not null will ignore
 * matches unless they're in a member with that name.
 *
 * The result is a table containing a list of matches.  The members of each
 * row are:
 *   index	The outermost array subscript of the match. Omitted if no array.
 *   key	The innermost member name of the match.  Omitted if no object.
 *   value	The matching value that was found.
 *   expr	Expression for the match, suitable for use with json_by_expr()
 *
 * If no matches are found, an empty array is returned.  Parameter errors cause
 * a "null" json_t to be returned containing an error message.
 */
static json_t *find(json_t *haystack, json_t *needle, int ignorecase, regex_t *regex, char *needkey)
{
	jsonfind_t find;

	/* Check parameters */
	if (haystack->type != JSON_ARRAY && haystack->type != JSON_OBJECT)
		return json_error_null(0, "Can only find within an object or array");
	if (!regex && (needle->type != JSON_STRING && needle->type != JSON_NUMBER))
		return json_error_null(0, "Can only find string, number, or regex");

	/* Fill in the find argument block */
	memset(&find, 0, sizeof find);
	find.needle = needle;
	find.regex = regex;
	find.needkey = needkey;
	find.ignorecase = ignorecase;
	find.index = -1;
	find.key = NULL;
	find.size = 100;
	find.used = 0;
	find.expr = (char *)malloc(find.size);
	find.result = json_array();

	/* If searching for a number, convert it to binary */
	if (needle && needle->type == JSON_NUMBER) {
		if (needle->text[0] == '\0') {
			find.int_or_double = needle->text[1];
		} else {
			if (strchr(find.needle->text, '.')
			 || strchr(find.needle->text, 'e')
			 || strchr(find.needle->text, 'E'))
				find.int_or_double = 'd';
			else
				find.int_or_double = 'i';
		}
		if (find.int_or_double == 'i') {
			find.needint = json_int(needle);
			find.needdouble = (double)find.needint;
		} else {
			find.needdouble = json_double(needle);
			/* don't need find->needint */
		}
	}

	/* Let the helper function do most of the work */
	help_find(haystack, &find);

	/* Clean up, and Return the result */
	free(find.expr);
	return find.result;
}

/* Do a deep search for a value */
json_t *json_find(json_t *haystack, json_t *needle, int ignorecase, char *needkey)
{
	return find(haystack, needle, ignorecase, NULL, needkey);
}

/* Do a deep search for a regular expression */
json_t *json_find_regex(json_t *haystack, regex_t *regex, char *needkey)
{
	return find(haystack, NULL, 0, regex, needkey);
}
