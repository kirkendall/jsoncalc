#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "json.h"
#include "calc.h"
#include "error.h"

/* This file defines the context functions.  Contexts are used in json_calc()
 * to provide access to outside data, in the manner of variables.  The
 * functions here let you manage a stack of contexts, and search for named
 * values defined within the context, and assign new values to variables.
 */

/* This datatype is used to maintain a list functions to call when creating
 * a new context.  The list is built by jsonc_context_hook, and used by
 * json_context_std().
 */
typedef struct contexthook_s {
	struct contexthook_s *next;
	jsoncontext_t *(*addcontext)(jsoncontext_t *context);
} contexthook_t;

static contexthook_t *extralayers = NULL;

/* Add a function which may add 0 ot more layers to the standard context.
 * This is mostly intended to allow plugs to define symbols that should
 * be globally accessible to jsoncalc.  The jsoncalc program itself may
 * use it for adding the autoload directory.
 */
void json_context_hook(jsoncontext_t *(*addcontext)(jsoncontext_t *context))
{
	contexthook_t *hook = (contexthook_t *)malloc(sizeof(contexthook_t));
	hook->addcontext = addcontext;
	hook->next = extralayers;
	extralayers = hook;
}


/* Free a context.  Also frees the data associated with it, unless
 * context->flags has the JSON_CONTEXT_NOFREE bit set.
 */
jsoncontext_t *json_context_free(jsoncontext_t *context)
{
        jsoncontext_t *older;

        /* Defend against NULL */
        if (!context)
                return NULL;

        /* If supposed to free data, do that */
        if ((context->flags & JSON_CONTEXT_NOFREE) == 0)
		json_free(context->data);

        /* Free the context */
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

/* This is an autoload handler for supplying the current date/time.  If the
 * time hasn't changed since the last call, it simply returns NULL so the
 * cached value can be reused.
 */
static json_t *stdcurrent(char *key)
{
	static time_t lastnow;
	static struct tm localtm, utctm;
	time_t now;
	char buf[30];

	/* For "now", return the current time as a number */
	if (!strcasecmp(key, "now")) {
		time(&now);
		return json_from_int((int)now);
	}

	/* Are we looking for a formatted time value? */
	if (!strcasecmp(key, "current_date")
	 || !strcasecmp(key, "current_time")
	 || !strcasecmp(key, "current_datetime")
	 || !strcasecmp(key, "current_timestamp")
	 || !strcasecmp(key, "current_tz")) {
		/* If the time has changed since the last call, then refresh
		 * the "struct tm" buffers.  Two things to note here: First,
		 * The "lastnow" variable is initialized to 0, and the current
		 * time certainly is not 0, so the first call will definitely
		 * fetch new time values.  Second, if multiple stacks use this
		 * function simultaneously then they'll be doing it for the
		 * same "now" value so there is no race condition.  Threadsafe!
		 */
		time(&now);
		if (now != lastnow) {
			(void)gmtime_r(&now, &utctm);
			(void)localtime_r(&now, &localtm);
			lastnow = now;
		}

		/* Format it as appropriate */
		if (!strcasecmp(key, "current_date")) {
			sprintf(buf, "%04d-%02d-%02d",
				localtm.tm_year + 1900,
				localtm.tm_mon + 1,
				localtm.tm_mday);
		} else if (!strcasecmp(key, "current_time")) {
			sprintf(buf, "%02d:%02d:%02d",
				localtm.tm_hour,
				localtm.tm_min,
				localtm.tm_sec);
		} else if (!strcasecmp(key, "current_datetime")) {
			sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d",
				localtm.tm_year + 1900,
				localtm.tm_mon + 1,
				localtm.tm_mday,
				localtm.tm_hour,
				localtm.tm_min,
				localtm.tm_sec);
		} else if (!strcasecmp(key, "current_timestamp")) {
			sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
				utctm.tm_year + 1900,
				utctm.tm_mon + 1,
				utctm.tm_mday,
				utctm.tm_hour,
				utctm.tm_min,
				utctm.tm_sec);
		} else /* current_timezone */ {
			int hours, minutes;

			/* Find the difference in hours and minutes between
			 * UTC and local time.  Start by assuming date is the
			 * same.
			 */
			hours = utctm.tm_hour - localtm.tm_hour;
			minutes = utctm.tm_min - localtm.tm_min;

			/* If date is actually different, adjust hours */
			if (utctm.tm_mday < localtm.tm_mday)
				hours -= 24;
			else if (utctm.tm_mday > localtm.tm_mday)
				hours += 24;

			/* We want minutes to be the same sign as hours.
			 * If different, adjust hours and minutes.
			 */
			if (hours * minutes < 0) { /* opposite signs */
				if (minutes < 0) {
					minutes += 60;
					hours--;
				} else {
					minutes -= 60;
					hours++;
				}
			}

			/* Format it */
			if (hours < 0)
				sprintf(buf, "-%02d:%02d", -hours, -minutes);
			else
				sprintf(buf, "+%02d:%02d", hours, minutes);
		}

		/* Return it as a string */
		return json_string(buf, -1);
	}

	/* No other special names */
	return NULL;
}

/* This creates the first several layers of a typical context stack.  "args"
 * should be an object containing members that should be accessible in scripts
 * via the "global.args" or simply "args" pseudovariables.  The "args" data
 * will be freed when the last context layer is freed.
 */
jsoncontext_t *json_context_std(json_t *args)
{
	json_t	*base, *global, *vars, *consts, *current_data;
	jsoncontext_t *context;
	contexthook_t *hook;

	/* Generate the data for the base context.  This is an object of the
	 * form {global:{vars:{},consts:{},args{},files:[],current_data:null}
	 */
	base = json_object();
	global = json_object();
	vars = json_object();
	consts = json_object();
	json_append(global, json_key("vars", vars));
	json_append(global, json_key("consts", consts));
	if (args)
		json_append(global, json_key("args", args));
	json_append(global, json_key("files", json_array()));
	json_append(base, json_key("global", global));
	current_data = json_null();
	json_append(base, json_key("current_data", current_data));

	/* Create the base layer of the context */
	context = json_context(NULL, base, JSON_CONTEXT_GLOBAL);

	/* Add a layer that autoloads the time variables.  These should not
	 * be cached.  Also add dummy versions of all time variables, mostly
	 * for the benefit of the name completion.
	 */
	context->autoload = stdcurrent;
	context->flags |= JSON_CONTEXT_NOCACHE;
	json_append(base, json_key("current_date", json_null()));
	json_append(base, json_key("current_time", json_null()));
	json_append(base, json_key("current_datetime", json_null()));
	json_append(base, json_key("current_timestamp", json_null()));
	json_append(base, json_key("current_tz", json_null()));

	/* Allow plugins or applications to add their own layers */
	for (hook = extralayers; hook; hook = hook->next)
		context = (*hook->addcontext)(context);

	/* Create a layer to serve as "this", containing the contents of
	 * the "current_data" symbol.  Since the data will be freed when
	 * the base context is freed or current_data is assigned a new value,
	 * we don't want to free it for this context.
	 */
	context = json_context(context, current_data, JSON_CONTEXT_GLOBAL | JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);

	/* Create a layer above that for the contents of "global", mostly the
	 * "vars" and "consts" symbols.  Since these will be freed when the
	 * lower level is freed, we don't want to free them when this layer
	 * is freed.
	 */
	context = json_context(context, global, JSON_CONTEXT_GLOBAL | JSON_CONTEXT_NOFREE);

	/* Create two or three more layers, exposing the contents of "vars"
	 * and "consts", and maybe "args".  Again, we don't free it when this
	 * context is freed because they're defined in a lower layer.
	 */
	if (args)
		context = json_context(context, args, JSON_CONTEXT_GLOBAL | JSON_CONTEXT_CONST | JSON_CONTEXT_NOFREE);
	context = json_context(context, consts, JSON_CONTEXT_GLOBAL | JSON_CONTEXT_CONST | JSON_CONTEXT_NOFREE);
	context = json_context(context, vars, JSON_CONTEXT_GLOBAL | JSON_CONTEXT_VAR | JSON_CONTEXT_NOFREE);

	/* DONE! Return the context stack. */
	return context;
}


/* Select a new current file, and optionally append a filename to the files
 * array.  The context must have been created by json_context_std() so this
 * function can know where to stuff the file info.  The "current" parameter
 * is either an index into the files array, or -1 to move forward 1, -2 to
 * stay on the same entry, or -3 to move back one entry.  Returns the files
 * array, which you do NOT need to free since it'll be freed when the context
 * is freed.
 */
json_t *json_context_file(jsoncontext_t *context, char *filename, int *refcurrent)
{
	jsoncontext_t *globals, *thiscontext;
	json_t	*files, *j, *f;
	int	current_file, noref, i;

	/* Defend against empty context */
	if (!context)
		return NULL;

	/* If refcurrent is NULL, then assume no move */
	if (!refcurrent) {
		refcurrent = &noref;
		*refcurrent = JSON_CONTEXT_FILE_SAME;
	}

	/* Locate the globals context at the bottom of the context stack */
	thiscontext = NULL;
	for (globals = context; globals->older; globals = globals->older) {
		/* Also look for the global "this" layer, which stores the
		 * context of the current file.
		 */
		if ((globals->flags & (JSON_CONTEXT_GLOBAL | JSON_CONTEXT_THIS)) == (JSON_CONTEXT_GLOBAL | JSON_CONTEXT_THIS))
			thiscontext = globals;
	}

	/* If no global "this" layer was found, it must not be a std context */
	if (!thiscontext)
		return NULL;

	/* Locate the "files" array within that context */
	files = json_by_expr(globals->data, "global.files", NULL);
	if (!files)
		return NULL;

	/* If given a filename, check for it in the list.  If it's there, then
	 * set *refcurrent to its index; else add it to the list.  Each file
	 * gets an object containing the name, a modified flag, potentially
	 * other info.
	 */
	if (filename) {
		/* Scan the files table for this filename */
		for (i = 0, j = files->first; j; j = j->next, i++)
			if ((f = json_by_key(j, "filename")) != NULL
			 && f->type == JSON_STRING
			 && !strcmp(f->text, filename))
				break;

		/* Did we find it? */
		if (j) {
			/* Select it as the current file */
			*refcurrent = i;
		} else {
			/* Create the basic entry */
			json_t *entry = json_object();
			json_append(entry, json_key("filename", json_string(filename, -1)));
			json_append(entry, json_key("modified", json_bool(0)));

			/* Add it to the "files" list */
			json_append(files, entry);

			/* Make it be the current file */
			*refcurrent = json_length(files) - 1;
		}
	}

	/* Get the current_file index, if any, and use that to adjust the
	 * "current" parameter.
	 */
	j = json_by_key(globals->data, "current_file");
	if (j) {
		current_file = json_int(j);
		if (*refcurrent < 0)
			*refcurrent = current_file + 2 + *refcurrent;
	} else {
		current_file = 0;
	}

	/* If "current" is out of range for the "files" array, clip it */
	if (*refcurrent < 0)
		*refcurrent = 0;
	else if (*refcurrent >= json_length(files))
		*refcurrent = json_length(files) - 1;

	/* Load the new current file, if the numbers are different or no
	 * file was loaded before.  Note that we don't need to explicitly
	 * free the old data, because it gets freed when we reassign the
	 * "current_data" member (the first json_append() below).
	 */
	if (current_file != *refcurrent || !j) {
		/* Load the data.  If it couldn't be loaded then say why */
		char *currentname = json_text_by_key(json_by_index(files, *refcurrent), "filename");
		json_t *data;
		if (!currentname)
			data = json_error_null(0, "There is no current file");
		else {
			data = json_parse_file(currentname);
			if (!data)
				data = json_error_null(0, "File \"%s\" is unreadable", currentname);
		}
		json_append(globals->data, json_key("current_data", data));
		thiscontext->data = data;

		/* Update "current_file" number */
		json_append(globals->data, json_key("current_file", json_from_int(*refcurrent)));
	}

	/* Return the list of files */
	return files;
}


/* This adds context layers for a user function call.  "fn" identifies the
 * function being called, mostly so we can see what arguments it uses.
 * "args" is an array containing the argument values.
 *
 * The context will contain COPIES of the argument values.  Those copies will
 * be automatically freed when the context layer is freed.
 */
jsoncontext_t *json_context_func(jsoncontext_t *context, jsonfunc_t *fn, json_t *args)
{
	json_t	*cargs, *name, *value;
	json_t	*vars, *consts;

	/* Build an object containing the actual arguments */
	cargs = json_copy(fn->userparams);
	for (name = fn->userparams->first, value = args->first;
	     name && value;
	     name = name->next, value = value->next) {
		json_append(cargs, json_key(name->text, json_copy(value)));
	}

	/* Also "vars" and "consts" to the arguments object */
	vars = json_object();
	json_append(cargs, json_key("vars", vars));
	consts = json_object();
	json_append(cargs, json_key("consts", consts));

	/* Add an "args" context layer containing those arguments */
	context = json_context(context, cargs, JSON_CONTEXT_ARGS);

	/* Add a "this" context containing the value of the first arg */
	if (args->first)
		context = json_context(context, json_copy(args->first), JSON_CONTEXT_THIS);

	/* Add other layers exposing the contents of "vars" and "consts".
	 * Since that data will be freed when the "args" layer is freed,
	 * we don't want to free them when these layers are freed.
	 */
	context = json_context(context, consts, JSON_CONTEXT_CONST | JSON_CONTEXT_NOFREE);
	context = json_context(context, vars, JSON_CONTEXT_VAR | JSON_CONTEXT_NOFREE);

	/* Return the modified context */
	return context;
}


/******************************************************************************/


/* Scan items in a context list for given name, and return its value.  If not
 * found, return NULL.  As special cases, the name "this" returns the most
 * recently added item with JSON_CONTEXT_THIS set, and "that" returns the
 * second-most-recent.
 *
 * Some context layers can have autoloaders.  If the name isn't found in the
 * layer already, then the autoloader is given a shot... except that if
 * reflayer is not null (used for assignments) then we skip that because
 * autoloaded items can't be modified.
 *
 * context is the top of the context stack to search.
 * key is the member name to search for.
 * reflayer, if not NULL, will be be set to the context layer containing key.
 * Also, a non-NULL reflayer implies that we're trying to assign, so only
 * contexts marked with JSON_CONTEXT_VAR or JSON_CONTEXT_CONST will be checked.
 */
json_t *json_context_by_key(jsoncontext_t *context, char *key, jsoncontext_t **reflayer)
{
        json_t  *val;
	int	firstthis;
	int	otherlocal;

        firstthis = 1;
        otherlocal = 0;
        while (context) {
		/* Skip if local to some other function */
		if ((context->flags & JSON_CONTEXT_GLOBAL) != 0)
			otherlocal = 0;
		if (otherlocal) {
			context = context->older;
			continue;
		}

		/* If we're looking to assign, skip context that isn't a
		 * "var" or "const" layer.
		 */
		if (reflayer && (context->flags & (JSON_CONTEXT_VAR|JSON_CONTEXT_CONST)) == 0) {
			context = context->older;
			continue;
		}

                /* "this" returns the most recently added item in its entirety*/
                if (context->flags & JSON_CONTEXT_THIS) {
			if (!strcasecmp(key, "this")
			 || (!strcasecmp(key, "that") && !firstthis)) {
				if (reflayer)
					*reflayer = context;
				return context->data;
			}
			firstthis = 0;
		}

                /* If there's an "autoload" handler and this layer doesn't
                 * cache results of autoloading, then give it a shot
                 */
                if (context->autoload && !reflayer && (context->flags & JSON_CONTEXT_NOCACHE) != 0) {
                        val = (*context->autoload)(key);
                        if (val) {
				/* Add it to the autoload object.  Since this
				 * layer doesn't cache results, the only reason
				 * for doing this is so the value can be freed
				 * later, when the context is freed.  That's
				 * a very good reason though!
				 */
                                json_append(context->data, json_key(key, val));
                                return val;
                        }
                }

                /* If context data is an object, check for a member */
                if (context->data->type == JSON_OBJECT) {
                        val = json_by_key(context->data, key);
                        if (val) {
				if (reflayer)
					*reflayer = context;
                                return val;
			}
                }

                /* If there's an "autoload" handler, give it a shot */
                if (context->autoload && !reflayer && (context->flags & JSON_CONTEXT_NOCACHE) == 0) {
                        val = (*context->autoload)(key);
                        if (val) {
				/* Add it to the autoload object */
                                json_append(context->data, json_key(key, val));
                                return val;
                        }
                }

                /* Not found here.  Try older contexts */
                if (context->flags & JSON_CONTEXT_ARGS)
			otherlocal = 1;
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
	case JE_CONST:		fmt = "Attempt to change const \"%s\""; break;
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
	jsoncontext_t	*layer, *scan;

	/* Find the context layer.  Also scan for duplicate name. */
	for (layer = NULL, scan = *refcontext; scan; scan = scan->older) {

		/* If this layer holds args/consts/vars, and one of them is
		 * the name we're trying to declare, then return 0.  This
		 * intentionally does NOT protect against the case where
		 * "this" contains a member with the name that you want to
		 * declare.
		 */
		if ((scan->flags & (JSON_CONTEXT_VAR | JSON_CONTEXT_CONST | JSON_CONTEXT_ARGS)) != 0
		 && json_by_key(scan->data, key))
			return 0;

		/* Stop after the args layer */
		if (scan->flags & JSON_CONTEXT_ARGS)
			break;

		/* If this is the layer we want to add a var/const to, then
		 * remember it.
		 */
		if (!layer && (scan->flags & flags) == flags)
			layer = scan;
	}

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
