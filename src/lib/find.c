/* find.c */
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <jx.h>

/* This stores the search criteria and a incremental results.  The help_find()
 * function is heavily recursive, so if we had to pass all of this as
 * parameters individually it'd be a big burden.
 */
typedef struct {
	jx_t	*needle;	/* String or number to search for */
	regex_t *regex;		/* Regular expression to search for */
	jxcalc_t *calc;	/* Expression to search for (RHS of @ operator) */
	jxcontext_t *context;	/* Context of the "calc" expression */
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
	jx_t	*result;	/* Table of found matches */
} jxfind_t;

/* Append a string to find->expr, expanding it if necessary.  When we move
 * deeper into a data structure, this is used to build the path up to whatever
 * part we're scanning now.
 */
static void help_find_cat(jxfind_t *find, char *str)
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

/* Append a row to the find->result table */
static void help_find_row(jxfind_t *find, jx_t *node)
{
	jx_t *found = jx_object();
	if (find->index >= 0)
		jx_append(found, jx_key("index", jx_from_int(find->index)));
	if (find->key)
		jx_append(found, jx_key("key", jx_string(find->key, -1)));
	jx_append(found, jx_key("value", jx_copy(node)));
	jx_append(found, jx_key("expr", jx_string(find->expr, find->used)));
	jx_append(find->result, found);
}

/* Do a deep search for a value.  This is a helper function for jfn_find(),
 * which implement's jx's find() function.
 */
static void help_find(jx_t *haystack, jxfind_t *find)
{
	jx_t	*scan;
	int	wasused, wasindex, i, match;
	char	*waskey;
	char	indexstr[40];

	/* If given a "calc" test and it matches, then add it to the result
	 * but DON'T continue to scan its contents for additional matches.
	 */
	if (find->calc) {
		jx_t *test = jx_calc(find->calc, find->context, NULL);
		match = jx_is_true(test);
		jx_free(test);
		if (match) {
			help_find_row(find, haystack);
			return;
		}
	}
	/* Arrays and objects are treated a bit differently */
	if (haystack->type == JX_ARRAY) {
		/* For each element... */
		for (i = 0, scan = jx_first(haystack); scan; i++, scan = jx_next(scan)) {
			/* If the value is an object or array, recurse */
			if (scan->type == JX_OBJECT || scan->type == JX_ARRAY) {
				/* Append this element's index to expr */
				wasused = find->used;
				snprintf(indexstr, sizeof indexstr, "[%d]", i);
				help_find_cat(find, indexstr);
				wasindex = find->index;
				if (find->index == -1)
					find->index = i;
				if (find->context)
					find->context = jx_context(find->context, scan, JX_CONTEXT_NOFREE|JX_CONTEXT_THIS);

				/* Recurse */
				help_find(scan, find);

				/* Restore expr */
				if (find->context)
					find->context = jx_context_free(find->context);
				find->used = wasused;
				find->index = wasindex;
				continue;
			} else if (find->calc) {
				/* Already did the calc test */
				continue;
			} else if (find->needkey && (!find->key || 0 != jx_mbs_casecmp(find->needkey, find->key) )) {
				/* Wrong key.  The only reason we're scanning
				 * this array is that it might have an element
				 * that's an object with needkey, but this
				 * element isn't an object/array.
				 */
				continue;
			} else if (!find->needle && !find->regex) {

				/* Arrays are weird in one way: When searching
				 * for "any value" (usually because we're only
				 * looking or a specific key) then we DON'T
				 * want to add each element to the result.
				 * We've already added the array as a whole
				 * if it is the value of a member with the
				 * desired key; that's enough.
				 */
				if (find->needkey)
					continue;
			} else if (scan->type == JX_STRING && find->regex) {
				/* Compare against the regexp */
				regmatch_t matches[10];
				if (regexec(find->regex, scan->text, 10, matches, 0) != 0)
					continue;
			} else if (scan->type == JX_STRING && find->needle->type == JX_STRING) {
				/* Compare as strings */
				if (find->ignorecase) {
					if (0 != jx_mbs_casecmp(find->needle->text, scan->text))
						continue;
				} else {
					if (0 != strcmp(find->needle->text, scan->text))
						continue;
				}
			} else if (scan->type == JX_NUMBER && find->needle->type == JX_NUMBER) {
				/* Does it match? */

				/* Optimization for comparing binary integers */
				if (scan->text[0] == 0 && scan->text[1] == 'i' && find->int_or_double == 'i') {
					/* Compare binary integers */
					if (jx_int(scan->first) != find->needint)
						continue;
				} else {
					/* Compare as double */
					if (jx_double(scan->first) != find->needdouble)
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
			help_find_row(find, scan);

			/* Restore expr */
			find->used = wasused;
			continue;
		}
	} else /* JX_OBJECT */ {
		/* For each member... */
		for (scan = haystack->first; scan; scan = scan->next) { /* object */
			/* If the value is an object or array, recurse */
			if (scan->first->type == JX_OBJECT || scan->first->type == JX_ARRAY) {
				/* Append this member's key to expr */
				wasused = find->used;
				help_find_cat(find, scan->text);

				/* If any value can match, and the key matches
				 * (or we don't care about the key) then add
				 * this member as a match.
				 */
				if (!find->needle
				 && !find->regex
				 && !find->calc
				 && (!find->needkey || !jx_mbs_casecmp(find->needkey, scan->text)))
					help_find_row(find, scan->first);

				/* Store the key so that if we're scanning an
				 * array, we'll know which array this is.
				 */
				waskey = find->key;
				find->key = scan->text;
				if (find->context)
					find->context = jx_context(find->context, scan, JX_CONTEXT_NOFREE|JX_CONTEXT_THIS);

				/* Recurse */
				help_find(scan->first, find);

				/* Restore expr */
				if (find->context)
					find->context = jx_context_free(find->context);
				find->key = waskey;
				find->used = wasused;
				continue;
			} else if (find->calc) {
				/* We already checked */
				continue;
			} else if (find->needkey && 0 != jx_mbs_casecmp(find->needkey, scan->text)) {
				/* Wrong key */
				continue;
			} else if (scan->first->type == JX_STRING && find->needle && find->needle->type == JX_STRING) {
				/* Compare as strings */
				if (find->ignorecase) {
					if (0 != jx_mbs_casecmp(find->needle->text, scan->first->text))
						continue;
				} else {
					if (0 != strcmp(find->needle->text, scan->first->text))
						continue;
				}
			} else if (scan->first->type == JX_STRING && find->regex) {
				/* Compare against the regexp */
				regmatch_t matches[10];
				if (regexec(find->regex, scan->first->text, 10, matches, 0) != 0)
					continue;
			} else if (scan->first->type == JX_NUMBER && find->needle && find->needle->type == JX_NUMBER) {
				/* Does it match? */

				/* Optimization for comparing binary integers */
				if (scan->first->text[0] == 0 && scan->first->text[1] == 'i' && find->int_or_double == 'i') {
					/* Compare binary integers */
					if (jx_int(scan->first) != find->needint)
						continue;
				} else {
					/* Compare as double */
					if (jx_double(scan->first) != find->needdouble)
						continue;
				}

			} else if (!find->needle && !find->regex && !find->calc) {
				/* any value matches */
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
			waskey = find->key;
			find->key = scan->text;
			help_find_row(find, scan->first);

			/* Restore expr */
			find->key = waskey;
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
 *   expr	Expression for the match, suitable for use with jx_by_expr()
 *
 * If no matches are found, an empty array is returned.  Parameter errors cause
 * a "null" jx_t to be returned containing an error message.
 */
static jx_t *find(jx_t *haystack, jx_t *needle, int ignorecase, regex_t *regex, char *needkey, jxcalc_t *calc, jxcontext_t *context)
{
	jxfind_t find;

	/* Check parameters */
	if (haystack->type != JX_ARRAY && haystack->type != JX_OBJECT)
		return jx_error_null(0, "Can only find within an object or array");
	if (!regex && needle && (needle->type != JX_STRING && needle->type != JX_NUMBER))
		return jx_error_null(0, "Can only find string, number, or regex");

	/* Fill in the find argument block */
	memset(&find, 0, sizeof find);
	find.needle = needle;
	find.regex = regex;
	find.needkey = needkey;
	find.ignorecase = ignorecase;
	find.calc = calc;
	find.context = context;
	find.index = -1;
	find.key = NULL;
	find.size = 100;
	find.used = 0;
	find.expr = (char *)malloc(find.size);
	find.result = jx_array();

	/* If searching for a number, convert it to binary */
	if (needle && needle->type == JX_NUMBER) {
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
			find.needint = jx_int(needle);
			find.needdouble = (double)find.needint;
		} else {
			find.needdouble = jx_double(needle);
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
jx_t *jx_find(jx_t *haystack, jx_t *needle, int ignorecase, char *needkey)
{
	return find(haystack, needle, ignorecase, NULL, needkey, NULL, NULL);
}

/* Do a deep search for a regular expression */
jx_t *jx_find_regex(jx_t *haystack, regex_t *regex, char *needkey)
{
	return find(haystack, NULL, 0, regex, needkey, NULL, NULL);
}

jx_t *jx_find_calc(jx_t *haystack, jxcalc_t *calc, jxcontext_t *context)
{
	return find(haystack, NULL, 0, NULL, NULL, calc, context);
}
