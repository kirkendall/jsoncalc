#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <jsoncalc.h>

/* Return the value of a named field within an object or array.  If there
 * is no such element, then return NULL.
 */
json_t *json_by_key(const json_t *container, const char *key)
{
	json_t *scan;
	char	*simple, *loose;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* Only objects should have named values (though JavaScript also
	 * supports "associative arrays" which do the same thing)
	 */
	if (container->type != JSON_OBJECT)
	{
		/* EEE "Attempt to find named item in a non-object"); */
		return NULL;
	}

	/* Scan for it.  If found, return its value */
	for (scan = container->first; scan; scan = scan->next) /* object */
	{
		assert(scan->type == JSON_KEY);
		if (!strcmp(scan->text, key))
			return scan->first;
	}

	/* Not found, but try again using loose name comparison */
	simple = strdup(key);
	(void)json_mbs_simple_key(simple, key);
	for (scan = container->first; scan; scan = scan->next) /* object */
	{
		/* Locate the key's "loose" version.  If it doesn't exist
		 * then create it now.
		 */
		loose = scan->text + strlen(scan->text) + 1;
		if (!*loose)
			(void)json_mbs_simple_key(loose, scan->text);

		/* Compare them now */
		if (!strcmp(loose, simple)) {
			free(simple);
			return scan->first;
		}
	}
	free(simple);

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
        for (scan = container->first; scan && !result; scan = scan->next) { /* object */
                if (scan->type == JSON_KEY && (scan->first->type == JSON_OBJECT || scan->first->type == JSON_ARRAY))
                        result = json_by_deep_key(scan->first, key);
                else if (scan->type == JSON_OBJECT || scan->type == JSON_ARRAY)
                        result = json_by_deep_key(scan, key);
        }
        return result;
}

/* Return the value of an indexed element within an array.  If there
 * is no such element, then return NULL.
 *
 * IMPORTANT NOTE: If container is a deferred array, then you must call
 * json_break() on the returned element when you're done with it.  If not a
 * deferred array, then json_break() is still safe to call on the element.
 */
json_t *json_by_index(json_t *container, int idx)
{
	json_t *scan;
	jsondef_t *def;
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

	/* If this is a deferred array, and it has a quick way to jump to a
	 * given index, then use that.
	 */
	if (json_is_deferred_array(container)
	 && (def = (jsondef_t *)(container->first))->fns->byindex)
		return (*def->fns->byindex)(container, idx);

	/* Scan for it.  If found, return its value */
	for (scan = json_first(container), scanidx = 0; scan; scan = json_next(scan))
	{
		/* if the index matches, use it */
		if (scanidx == idx)
			return scan;
		scanidx++;
	}

	/* Not found */
	return NULL;
}


/* Find an element of an array that contains a member with a given name and
 * optionally a given value for that name.  If found, return it; else return
 * NULL.
 * 
 * IMPORTANT NOTE: If container is a deferred array, then you must call
 * json_break() on the returned element when you're done with it.  If not a
 * deferred array, then json_break() is still safe to call on the element.
 */
json_t *json_by_key_value(json_t *container, const char *key, json_t *value) 
{
	json_t	*scan, *found;
	jsondef_t *def;

	/* This only works on tables (arrays of objects) */
	if (!json_is_table(container))
		return NULL;

	/* If it is a deferred array, and it has a "bykey" function pointer,
	 * then use that.
	 */
	if (json_is_deferred_array(container)
	 && (def = (jsondef_t*)container->first)->fns->bykeyvalue)

		return (*def->fns->bykeyvalue)(container, key, value);

	/* Scan array for element with that member key:value */
	for (scan = json_first(container); scan; scan = json_next(scan)) {
		if (scan->type != JSON_OBJECT)
			continue;
		found = json_by_key(scan, key);
		if (found && json_equal(found, value)) {
			return scan;
		}
	}

	/* Not found */
	return NULL;
}

/* Return an item inside nested objects or arrays, selected via a
 * JavaScript-like expression.  The expr is a string containing a series of
 * member names or subscript numbers.  Each of these values may be delimited
 * by one or more characters from the list ".[]", with the idea being that
 * you would use something like "ROList.ro[0].job[0].opcode" to fetch the
 * first opcode of the first RO.
 * 
 * The expression ends at the first character that isn't a letter, digit,
 * or one of "_[].".  If the "next" argument isn't null, the pointer that
 * it refers to will be set to the first character after the expression;
 * this way you can write wrappers to handle things such as comma-delimited
 * lists of expressions.
 *
 * Deferred arrays cause problems.  We want to return the json_t within the
 * container, but deferred arrays allocate and free elements as they are
 * scanned.  So if a deferred array is involved then the returned item must
 * look like a deferred element that json_break() can clean up.
 */
json_t *json_by_expr(json_t *container, const char *expr, const char **next)
{
	char	key[100];
	int	i, deep, quote;
	json_t	*step;
	json_t	*defelem;

	/* Defend against NULL */
	if (!container)
		return NULL;

	/* We need to keep track of whether the returned data is part of a
	 * deferred element.  If so, then we have some extra work to do before
	 * returning it.  For now, assume there is no deferred array.
	 */
	defelem = 0;

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
				if (defelem)
					json_break(defelem);
			        return NULL;
			}
			step = json_by_index(container, atoi(expr));
			if (!step) {
				if (defelem)
					json_break(defelem);
				return NULL;
			}
			while (isdigit(*expr))
				expr++;

			/* If this is an element of a deferred array, remember
			 * that so we can clean up later.
			 */
			if (json_is_deferred_element(step) && !defelem)
				defelem = step;
		}
		else if (isalpha(*expr) || *expr == '_')
		{
			if (container->type != JSON_OBJECT)
			{
			        /* EEE if (json_debug_flags.expr) json_throw(NULL, "Attempt to find a member in a non-object via an expr");*/
				if (defelem)
					json_break(defelem);
			        return NULL;
			}
			for (i = 0; i < sizeof key - 1 && (isalnum(*expr) || *expr == '_'); i++)
				key[i] = *expr++;
			key[i] = '\0';
			if (deep)
				step = json_by_deep_key(container, key);
			else
				step = json_by_key(container, key);
		}
		else if (*expr == '"' || *expr == '`')
		{
			if (container->type != JSON_OBJECT)
			{
			        /* EEE if (json_debug_flags.expr) json_throw(NULL, "Attempt to find a member in a non-object via an expr"); */
				if (defelem)
					json_break(defelem);
			        return NULL;
			}
			quote = *expr++;
			for (i = 0; i < sizeof key - 1 && *expr != quote; i++)
				key[i] = *expr++;
			key[i] = '\0';
			expr++;
			if (deep)
				step = json_by_deep_key(container, key);
			else
				step = json_by_key(container, key);
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
		if (!step && !next)
			break;
		container = step;
	} while (*expr && strchr("[].~", *expr));

	/* If we were in a deferred array, then we need to make the returned
	 * json_t look like an element of that deferred array.
	 */
	if (defelem && !json_is_deferred_element(container)) {
		/* We need to call json_break() on the defelem to free up
		 * the scanning resources.  Before we do that, though, we
		 * need to make a copy of the returned value.
		 */
		container = json_copy(container);

		/* The copy should have its ->next pointer going to a generic
		 * JSON_DEFER node, just so json_break() will free it.
		 */
		container->next = json_defer(NULL);

		/* Okay, now it's safe to free the deferred element */
		json_break(defelem);
	}

	/* return the result */
	if (next)
		*next = expr;
	return container;
}
