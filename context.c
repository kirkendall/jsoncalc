#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "json.h"

/* This file defines the context functions.  Contexts are used in json_calc()
 * to provide access to outside data, as though they were variables.  The
 * functions here let you manage a stack of contexts, and search for named
 * values defined within the context.
 */

/* Free a context.  Returns the next older context, or NULL if none.
 * Optionally frees the data associated with the context ("this").
 */
jsoncontext_t *json_context_free(jsoncontext_t *context, int freedata)
{
        jsoncontext_t *older;

        /* Defend against NULL */
        if (!context)
                return NULL;

        /* If supposed to free data, do that */
        if (freedata)
		json_free(context->data);

        /* Free it */
        older = context->older;
        free(context);

        return older;
}

/* Add a context.  Optionally add a handler for when json_context_by_key()
 * is used to search for a name that can't be found in data.
 */
jsoncontext_t *json_context(jsoncontext_t *older, json_t *data, json_t *(*autoload)(char *key))
{
        jsoncontext_t *context;

        /* Allocate it */
        context = (jsoncontext_t *)malloc(sizeof(jsoncontext_t));
        memset(context, 0, sizeof(jsoncontext_t));

        /* Initialize it.  We leave the hash uninitialized until we need it */
        context->older = older;
        context->data = data;
        context->autoload = autoload;

        /* Return it */
        return context;
}

/* Scan items in a context list for given name, and return its value.  If not
 * found, return NULL.  As special cases, the name "this" returns the most
 * recently added item and "that" returns the second-most-recent.
 */
json_t *json_context_by_key(jsoncontext_t *context, char *key)
{
        json_t  *val;

        while (context) {
                /* "this" returns the most recently added item in its entirety*/
                if (!strcmp(key, "this"))
                        return context->data;

                /* "that" returns the next older item */
                if (!strcmp(key, "that") && context->older)
                        return context->older->data;

                /* If "this" is an object, check for a member */
                if (context->data->type == JSON_OBJECT) {
                        val = json_by_key(context->data, key);
                        if (val)
                                return val;
                }

                /* If there's an "autoload" handler, give it a shot */
                if (context->autoload) {
                        val = (*context->autoload)(key);
                        if (val) {
				/* Add it to the autonames object */
                                if (context->data->type == JSON_OBJECT)
                                        json_append(context->data, json_key(key, val));
                                return val;
                        }
                }

                /* Not found here.  Try older contexts */
                context = context->older;
        }

        /* Nope, not in any context */
        return NULL;
}

/* Returns the default table for a "SELECT" statement.
 *
 * In SQL "SELECT" statements, the "FROM" clause is optional.  If it is omitted
 * then we need to choose a default table.  If "this" is a table, that's what
 * we want.  Otherwise we scan for the OLDEST table in the context, which is
 * probably a file from the command line.  If we don't find one, then we scan
 * for the oldest object containing a "filename" member, and then scan for am
 * array in the members of that.  If no table is found anywhere, return NULL.
 *
 * The returned json_t is part of the context, so you can assume it'll be
 * freed as part of the context.  You don't need to free it, but you can't
 * modify it either.
 */
json_t *json_context_default_table(jsoncontext_t *context)
{
	jsoncontext_t *scan;
	json_t	*found;

	/* Defend against NULL */
	if (!context)
		return NULL;

	/* If "this" is a table, use it */
	if (json_is_table(context->data))
		return context->data;

	/* Scan for the oldest context that is a table, and use that.  It is
	 * probably a file from the command line.
	 */
	for (found = NULL, scan = context->older; scan; scan = scan->older) {
		if (json_is_table(scan->data))
			found = scan->data;
	}
	if (found)
		return found;


	/* Scan for the oldest context that's an object containing a "filename"
	 * member.  This too is probably an a file from the command line.
	 */
	for (found = NULL, scan = context->older; scan; scan = scan->older) {
		if (scan->data->type == JSON_OBJECT && json_by_key(scan->data, "filename"))
			found = scan->data;
	}
	if (found) {
		/* Now scan that object for a member that's a table */
		for (found = found->first; found; found = found->next) {
			if (json_is_table(found->first))
				return found->first;
		}
	}

	/* No joy */
	return NULL;
}
