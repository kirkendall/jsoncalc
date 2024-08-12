#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "json.h"
#include "calc.h"
#include "error.h"

/* This file defines the context functions.  Contexts are used in json_calc()
 * to provide access to outside data, as though they were variables.  The
 * functions here let you manage a stack of contexts, and search for named
 * values defined within the context, and assign new values to variables.
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
jsoncontext_t *json_context(jsoncontext_t *older, json_t *data, jsoncontextflags_t flags)
{
        jsoncontext_t *context;

        /* Allocate it */
        context = (jsoncontext_t *)malloc(sizeof(jsoncontext_t));
        memset(context, 0, sizeof(jsoncontext_t));

        /* Initialize it.  We leave the hash uninitialized until we need it */
        context->older = older;
        context->data = data;
        context->flags = flags;

        /* Return it */
        return context;
}

/* Locate a context layer that satisfies the "flags" argument.  If no such
 * layer exists, then insert it into the context stack in an appropriate
 * position.  Returns the new context, and may also adjusst the top of the
 * context stack -- notice that we pass a reference to the stack pointer
 * instead of the pointer itself like we do for most context functions.
 *
 * "flags" can be one of the following:
 *   JSON_CONTEXT_GLOBAL_VAR	The layer for global variables
 *   JSON_CONTEXT_GLOBAL_CONST	The layer for global constants
 *   JSON_CONTEXT_ARGS		A new layer for function arguments
 *   JSON_CONTEXT_LOCAL_VAR	A layer for variables within a function
 *   JSON_CONTEXT_LOCAL_CONST	A layer for constants within a function
 */
jsoncontext_t *json_context_insert(jsoncontext_t **refcontext, jsoncontextflags_t flags)
{
	jsoncontext_t *scan, *lag;

	/* For JSON_CONTEXT_ARGS we always allocate a new layer on top */
	if (flags == JSON_CONTEXT_ARGS) {
		*refcontext = json_context(*refcontext, json_object(), flags);
		return *refcontext;
	}

	/* Local? */
	if ((flags & JSON_CONTEXT_GLOBAL) == 0) {
		/* Scan for an existing layer that works */
		for (lag = NULL, scan = *refcontext;
		     scan && scan->flags != JSON_CONTEXT_ARGS;
		     lag = scan, scan = scan->older) {
			if (((flags ^ scan->flags) & (JSON_CONTEXT_VAR|JSON_CONTEXT_CONST)) == 0)
				/* We found an existing layer we can use */
				return scan;
		}

		/* No layer found, so insert one between "lag" and "scan" */
		scan = json_context(scan, json_object(), flags);
		if (lag)
			lag->older = scan;
		else
			*refcontext = scan;
		return scan;
	}

	/* Global!  Scan for an existing global layer that works */
	for (lag = NULL, scan = *refcontext;
	     scan;
	     lag = scan, scan = scan->older) {
		if (((flags ^ scan->flags) & (JSON_CONTEXT_VAR|JSON_CONTEXT_CONST|JSON_CONTEXT_GLOBAL)) == 0)
			/* We found an existing layer we can use */
			return scan;
	}

	/* No layer found, so insert one between "lag" and "scan" */
	scan = json_context(scan, json_object(), flags);
	if (lag)
		lag->older = scan;
	else
		*refcontext = scan;
	return scan;
}

/* Scan items in a context list for given name, and return its value.  If not
 * found, return NULL.  As special cases, the name "this" returns the most
 * recently added item and "that" returns the second-most-recent.
 *
 * Some context layers can have autoloaders.  If the name isn't found in the
 * layer already, then the autoloader is given a shot... except that if
 * reflayer is not null (used for assignments) then it isn't.
 *
 * context is the top of the context stack to search.
 * key is the member name to search for.
 * reflayer, if not NULL, will be be set to the context layer containing key.
 */
json_t *json_context_by_key(jsoncontext_t *context, char *key, jsoncontext_t **reflayer)
{
        json_t  *val;
	int	firstthis;

        firstthis = 1;
        while (context) {
                /* "this" returns the most recently added item in its entirety*/
                if (context->flags & JSON_CONTEXT_THIS) {
			if (!strcmp(key, "this")
			 || (!strcmp(key, "that") && !firstthis)) {
				if (reflayer)
					*reflayer = context;
				return context->data;
			}
			firstthis = 0;
		}

                /* If "this" is an object, check for a member */
                if (context->data->type == JSON_OBJECT) {
                        val = json_by_key(context->data, key);
                        if (val) {
				if (reflayer)
					*reflayer = context;
                                return val;
			}
                }

                /* If there's an "autoload" handler, give it a shot */
                if (context->autoload && !reflayer) {
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

/* For a given L-value, find its layer, container, and value.  This is used
 * for assigning or appending to variables.  Return 0 on success, or -1 on
 * success when *refkey needs to be freed, or an error code on failure.
 *
 * That "-1" business is hack.  When assigning to a member of an object,
 * *refkey should identify the member to be assigned.  But when assigning by
 * a subscript ("object[string]" instead of "object.key") then the key is a
 * computed value, and we evaluate/free the subscript before returning, so we
 * need to make a dynamically-allocated copy of the key.  And then the calling
 * function needs to free it.  I decided to make this function be static just
 * because that hack is so obnoxious, at least this way it's only used by a
 * couple of functions in the same source file.
 */
static int jxlvalue(jsoncalc_t *lvalue, jsoncontext_t *context, jsoncontext_t **reflayer, json_t **refcontainer, json_t **refvalue, char **refkey)
{
	json_t	*value, *t, *v, *m;
	char	*skey;
	jsoncalc_t *sub;
	int	err, ret;
	jsoncontext_t *layer;

	switch (lvalue->op) {
	case JSONOP_NAME:
	case JSONOP_LITERAL:
		/* Literal can only be a string, serving as a quoted name */
		if (lvalue->op == JSONOP_LITERAL && lvalue->u.literal->type != JSON_STRING)
			return JE_BAD_LVALUE;

		/* Get the name */
		if (lvalue->op == JSONOP_NAME)
			*refkey = lvalue->u.text;
		else
			*refkey = lvalue->u.literal->text;

		/* Look for it in the context */
		value = json_context_by_key(context, *refkey, &layer);

		/* If found, celebrate */
		if (!value)
			return JE_UNKNOWN_VAR;
		if (reflayer)
			*reflayer = layer;
		if (refcontainer)
			*refcontainer = layer->data;
		if (refvalue)
			*refvalue = value;
		return JE_OK;

	case JSONOP_DOT:
		/* Recursively look up the left side of the dot */
		if ((err = jxlvalue(lvalue->u.param.left, context, reflayer, refcontainer, &value, refkey)) > JE_OK)
			return err;
		if (err < 0) {
			free(*refkey);
			return JE_UNKNOWN_MEMBER;
		}
		if (value == NULL)
			return JE_UNKNOWN_VAR;

		/* If lhs of dot isn't an object, that's a problem */
		if (value->type != JSON_OBJECT)
			return JE_NOT_OBJECT;

		/* For DOT, the right parameter should just be a name */
		if (lvalue->u.param.right->op == JSONOP_NAME)
			*refkey = lvalue->u.param.right->u.text;
		else if (lvalue->u.param.right->op == JSONOP_LITERAL
		      && lvalue->u.param.right->u.literal->type == JSON_STRING)
			*refkey = lvalue->u.param.right->u.literal->text;
		else
			/* invalid rhs of a dot */
			return JE_NOT_KEY;

		/* Look for the name in this object.  If not found, "t" will
		 * be null, which is a special type of failure.  This failure
		 * only happens if we expected to find a value (e.g., to append)
		 */
		t = json_by_key(value, *refkey);
		if (!t && !refvalue)
			return JE_UNKNOWN_MEMBER;

		/* The value becomes the container, and "t" becomes the value */
		if (refcontainer)
			*refcontainer = value;
		if (refvalue)
			*refvalue = t;
		return JE_OK;

	case JSONOP_SUBSCRIPT:
		/* Recursively look up the left side of the subscript */
		if ((err = jxlvalue(lvalue->u.param.left, context, reflayer, refcontainer, &value, refkey)) > JE_OK)
			return err;
		if (err < 0) {
			free(*refkey);
			return JE_UNKNOWN_MEMBER;
		}
		if (value == NULL)
			return JE_UNKNOWN_VAR;

		/* The [key:value] style of subscripts is handled specially */
		sub = lvalue->u.param.right;
		if (sub->op == JSONOP_COLON) {
			/* The array[key:value] case */

			/* Get the key */
			if (sub->u.param.left->op == JSONOP_NAME)
				skey = sub->u.param.left->u.text;
			else if (sub->u.param.left->op == JSONOP_LITERAL
			 && sub->u.param.left->u.literal->type == JSON_STRING)
				skey = sub->u.param.left->u.literal->text;
			else /* invalid key */
				return JE_BAD_SUB_KEY;

			/* Evaluate the value */
			t = json_calc(sub->u.param.right, context, NULL);
			/*!!! should I try to detect "null" with error text? */

			/* Scan the array for an element with that member key
			 * and value.
			 */
			for (v = value->first; v; v = v->next) {
				if (v->type == JSON_OBJECT) {
					m = json_by_key(v, skey);
					if (m && json_equal(m, t))
						break;
				}
			}

			/* If not found, fail */
			if (!v)
				return JE_UNKNOWN_SUB;

			/* Return what we found */
			if (refcontainer)
				*refcontainer = value;
			if (refvalue)
				*refvalue = v;
			return JE_OK;

		} else {
			/* Use json_calc() to evaluate the subscript */
			t = json_calc(lvalue->u.param.right, context, NULL);

			/* Look it up.  array[number] or object[string] */
			if (value->type == JSON_ARRAY && t->type == JSON_NUMBER)
				v = json_by_index(value, json_int(t));
			else if (value->type == JSON_OBJECT && t->type == JSON_STRING) {
				*refkey = t->text;
				v = json_by_key(value, *refkey);
			} else {
				/* Bad subscript */
				json_free(t);
				return JE_BAD_SUB;
			}

			/* Okay, we're done with the subscript "t"... EXCEPT
			 * that if it's a string that will serve as an array
			 * subscript then we need to keep it the string for
			 * a while.
			 */
			ret = JE_OK;
			if (t->type == JSON_STRING && refkey) {
				*refkey = strdup(t->text);
				ret = -1;
			}
			json_free(t);

			/* Not finding a value is bad... unless we don't need
			 * one.  Its okay to assign new members to an existing
			 * object.
			 */
			if (!v && (*refvalue) && value->type != JSON_OBJECT)
				return JE_UNKNOWN_SUB;

			/* Return what we found */
			if (refcontainer)
				*refcontainer = value;
			if (refvalue)
				*refvalue = v;
			return ret;
		}

	default:
		/* Invalid operation in an l-value */
		return JE_BAD_LVALUE;
	}
}

/* Return a "null" json_t for a given error code */
static json_t *jxerror(int code, char *key)
{
	char	*fmt;
	switch (code) {
	case JE_BAD_LVALUE:	fmt = "Invalid assignment";	break;
	case JE_UNKNOWN_VAR:	fmt = "Unknown variable \"%s\"";	break;
	case JE_NOT_OBJECT:	fmt = "Attempt to access member in a non-object";	break;
	case JE_NOT_KEY:	fmt = "Attempt to use a non-key as a member label";	break;
	case JE_UNKNOWN_MEMBER:	fmt = "Object has no member \"%s\"";	break;
	case JE_BAD_SUB_KEY:	fmt = "Invalid key for [key:value] subscript";	break;
	case JE_UNKNOWN_SUB:	fmt = "No element found with requested subscript";	break;
	case JE_BAD_SUB:	fmt = "Subscript as invalid type";	break;
	default: return json_null();
	}
	return json_error_null(code, fmt, key);
}

/* Assign a variable.  Returns NULL on success, or a json_t "null" with an
 * error message if it fails.
 *
 * "rvalue" will be freed when the context is freed.  This function DOES NOT
 * make a copy of it; it uses "rvalue" directly.  If that's an issue then the
 * calling function should make a copy.
 */
json_t *json_context_assign(jsoncalc_t *lvalue, json_t *rvalue, jsoncontext_t *context)
{
	jsoncontext_t	*layer;
	json_t	*container, *value, *scan;
	char	*key;
	int	err;

	/* We can't use a value that's already part of something else. */
	assert(rvalue->next == NULL);

	/* Get the details on what to change.  Note that for new members,
	 * value may be NULL even if jxlvalue() returns 1.
	 */
	if ((err = jxlvalue(lvalue, context, &layer, &container, &value, &key)) > JE_OK)
		return jxerror(err, key);

	/* If it's const then fail */
	if (layer->flags & JSON_CONTEXT_CONST) {
		if (err < 0)
			free(key);
		return jxerror(JE_CONST, key);
	}

	/* Objects are easy, arrays are hard */
	if (container->type == JSON_OBJECT) {
		json_append(container, json_key(key, rvalue));
		if (err < 0)
			free(key);
	} else {
		if (!value)
			return jxerror(JE_UNKNOWN_SUB, key);

		/* Use the tail of the array with the new value */
		rvalue->next = value->next;

		/* Replace either the head of the array or a later element */
		if (value == container->first) {
			/* Replace the first element with a new one */
			container->first = rvalue;
		} else {
			/* Scan for the element before the changed one */
			for (scan = container->first; scan->next != value; scan = scan->next) {
			}

			/* Replace the element after it with a new one */
			scan->next = rvalue;
		}

		/* Free the old value (but not its siblings) */
		value->next = NULL;
		json_free(value);
	}

	/* If this layer has a callback for modifications, call it */
	if (layer->modified)
		(*layer->modified)(layer, lvalue);

	/* All good! */
	return NULL;
}


/* Append to an array variable.  Returns NULL on success, or a json_t "null"
 * with an error message if it fails.
 *
 * "rvalue" will be freed when the context is freed.  This function DOES NOT
 * make a copy of it; it uses "rvalue" directly.  If that's an issue then the
 * calling function should make a copy.
 */
json_t *json_context_append(jsoncalc_t *lvalue, json_t *rvalue, jsoncontext_t *context)
{
	jsoncontext_t	*layer;
	json_t	*container, *value;
	char	*key;
	int	err;

	/* We can't use a value that's already part of something else. */
	assert(rvalue->next == NULL);

	/* Get the details on what to change */
	if ((err = jxlvalue(lvalue, context, &layer, &container, &value, &key)) != JE_OK)
		return jxerror(err, key);
	if (value == NULL)
		return jxerror(JE_UNKNOWN_VAR, key);

	/* If it's const then fail */
	if (layer->flags & JSON_CONTEXT_CONST)
		return jxerror(JE_CONST, key);

	/* We can only append to arrays */
	if (value->type != JSON_ARRAY)
		return json_error_null(JE_APPEND, "Can't append to %s \"%s\"", json_typeof(value, 0), key);

	/* Append! */
	json_append(value, rvalue);

	/* If this layer has a callback for modifications, call it */
	if (layer->modified)
		(*layer->modified)(layer, lvalue);

	return NULL;
}

/* Add a variable.  If necessary (as indicated by "flags") then add a context
 * for it too.  Return maybe-changed top of the context stack.  "flags" can be
 * one of the following:
 *   JSON_CONTEXT_GLOBAL_VAR	A global variable
 *   JSON_CONTEXT_GLOBAL_CONST	A global constant
 *   JSON_CONTEXT_LOCAL_VAR	A local variable
 *   JSON_CONTEXT_LOCAL_CONST	A local constant
 *
 * Return 1 on success, or 0 if the key was already declared.
 */
int json_context_declare(jsoncontext_t **refcontext, char *key, json_t *value, jsoncontextflags_t flags)
{
	jsoncontext_t	*layer;

	/* Find/add the context layer */
	layer = json_context_insert(refcontext, flags);

	if (json_by_key(layer->data, key))
		return 0;

	/* Add it */
	json_append(layer->data, json_key(key, value ? value : json_null()));
	return 1;
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
