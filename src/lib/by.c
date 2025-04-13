#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "json.h"

/* Return the value of a named field within an object or array.  If there
 * is no such element, then return NULL.
 */
json_t *json_by_key(const json_t *container, const char *key)
{
	json_t *scan;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* Only objects should have named values (though JavaScript also
	 * supports "associative arrays" which do the same thing)
	 */
	if (container->type != JSON_OBJECT && container->type != JSON_ARRAY)
	{
		/* EEE "Attempt to find named item in a non-object"); */
		return NULL;
	}

	/* Scan for it.  If found, return its value */
	for (scan = container->first; scan; scan = scan->next)
	{
		if (scan->type == JSON_KEY && !strcmp(scan->text, key))
			return scan->first;
	}

	/* Not found, but try again case-insensitively */
	for (scan = container->first; scan; scan = scan->next)
	{
		if (scan->type == JSON_KEY && !strcasecmp(scan->text, key))
			return scan->first;
	}

	/* Not found */
	return NULL;
}

/* Look for a member by key.  If not in the top-level object, then look
 * in anything that it contains.  Also, the container can be an array, not
 * just an object.
 */
json_t *json_by_deep_key(json_t *container, char *key)
{
        json_t  *result, *scan;

        /* First try the container itself (no nesting) */
        if (container->type == JSON_OBJECT) {
                result = json_by_key(container, key);
                if (result)
                        return result;
        }

        /* Next try any containers within this object */
        result = NULL;
        for (scan = container->first; scan && !result; scan = scan->next) {
                if (scan->type == JSON_KEY && (scan->first->type == JSON_OBJECT || scan->first->type == JSON_ARRAY))
                        result = json_by_deep_key(scan->first, key);
                else if (scan->type == JSON_OBJECT || scan->type == JSON_ARRAY)
                        result = json_by_deep_key(scan, key);
        }
        return result;
}

/* Return the value of an indexed element within an array.  If there
 * is no such element, then return NULL.
 */
json_t *json_by_index(json_t *container, int idx)
{
	json_t *scan;
	int	scanidx;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* Only arrays should have indexed values */
	if (container->type != JSON_ARRAY)
	{
		/* EEE "Attempt to find indexed item in a non-array" */
		return NULL;
	}

	/* Defend against negative indexes.  But also try to use them as being
	 * an index relative to the end of the array.
	 */
	if (idx < 0) {
		idx += json_length(container);
		if (idx < 0)
			return NULL;
	}

	/* Scan for it.  If found, return its value */
	for (scan = container->first, scanidx = 0; scan; scan = scan->next)
	{
		/* skip named elements */
		if (scan->type == JSON_KEY)
			continue;

		/* if the index matches, use it */
		if (scanidx == idx)
			return scan;
		scanidx++;
	}

	/* Not found */
	return NULL;
}

/* Return an item inside nested objects or arrays, selected via a
 * JavaScript-like expression.  The expr is a string containing a series of
 * symbols or non-negative numbers.  Each of these values may be delimited
 * by one or more characters from the list ".[]", with the idea being that
 * you would use something like "ROList.ro[0].job[0].opcode" to fetch the
 * first opcode of the first RO.
 * 
 * The expression ends at the first character that isn't a letter, digit,
 * or one of "_[].".  If the "next" argument isn't null, the pointer that
 * it refers to will be set to the first character after the expression;
 * this way you can write wrappers to handle things such as comma-delimited
 * lists of expressions.
 */
json_t *json_by_expr(json_t *container, char *expr, char **next)
{
	char	key[100];
	int	i, deep;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* Work through the expr, and down into the container */
	do
	{
		/* Skip leading delimiters */
		deep = 0;
		while (*expr && strchr("[].~", *expr)) {
			if (expr[0] == '.' && expr[1] == '.')
				deep = 1;
			expr++;
		}

		/* Detect number or symbol.  If neither, we're done */
		if (isdigit(*expr))
		{
			if (container->type != JSON_ARRAY)
			{
			        /* EEE if (json_debug_flags.expr) "Attempt to use an index on a non-array via an expr");*/
			        return NULL;
			}
			container = json_by_index(container, atoi(expr));
			if (!container)
				return NULL;
			while (isdigit(*expr))
				expr++;
		}
		else if (isalpha(*expr) || *expr == '_')
		{
			if (container->type != JSON_OBJECT)
			{
			        /* EEE if (json_debug_flags.expr) json_throw(NULL, "Attempt to find a member in a non-object via an expr");*/
			        return NULL;
			}
			for (i = 0; i < sizeof key - 1 && (isalnum(*expr) || *expr == '_'); i++)
				key[i] = *expr++;
			key[i] = '\0';
			if (deep)
				container = json_by_deep_key(container, key);
			else
				container = json_by_key(container, key);
		}
		else if (*expr == '"')
		{
			if (container->type != JSON_OBJECT)
			{
			        /* EEE if (json_debug_flags.expr) json_throw(NULL, "Attempt to find a member in a non-object via an expr"); */
			        return NULL;
			}
			expr++;
			for (i = 0; i < sizeof key - 1 && *expr != '"'; i++)
				key[i] = *expr++;
			key[i] = '\0';
			expr++;
			if (deep)
				container = json_by_deep_key(container, key);
			else
				container = json_by_key(container, key);
		}
		else
		{
			break;
		}

		/* NOTE: If we couldn't find what we're looking for, then
		 * container gets set to NULL and stays that way.  We continue
		 * parsing the expression anyway so we can find the end of it
		 * if there's a "next" pointer.
		 */
		if (!container && !next)
			break;
	} while (*expr && strchr("[].~", *expr));

	/* return the result */
	if (next)
		*next = expr;
	return container;
}
