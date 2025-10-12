#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <regex.h>
#include <jsoncalc.h>

/* Use the real version of json_calc here, not the debugging macro */
#ifdef json_calc
# undef json_calc
#endif

/* BIG NOTE ABOUT MEMORY MANAGEMENT
 *
 * My goals for memory management in json_calc() are that it should be simple
 * and efficient.  "Efficient" here means that it usually won't allocate new
 * json_t's if it doesn't have to.
 *
 * To keep it simple though, json_calc() always returns a freshly allocated
 * json_t tree.  If you call json_calc(), then you must eventually call
 * json_free() on the returned value.
 *
 * json_calc() is recursive, so it will sometimes allocate and free temporary
 * json_t's.  Some instances where it DOESN'T need to free a json_t that it
 * uses are:
 *
 *   Literals.  These are in the jsoncalc_t expression as JSONOP_LITERAL
 *		nodes.  They contain a json_t tree that's allocated by
 *		json_calc_parse() and freed by json_calc_free().
 *
 *   Names.	The values associated with names come from the context,
 *		usually retrieved via json_context_by_key().  They are
 *		allocated by the code that sets up the context, and freed
 *		when the context is freed.  Expressions sometimes create
 *		local contexts and free them, but that's a separate thing.
 *
 * That's all! So when json_calc() needs to access a left or right operand,
 * if it can fetch a literal or name then it doesn't need to free it;
 * otherwise (when it must recursively call json_calc()) it must free
 * the temporary values.
 */

/* These make accessing the left and right operands easier/clearer. */
#define LEFT  u.param.left
#define RIGHT u.param.right

/* These two macros fetch the left and right operands.  They always set the
 * "left" and "right" variables.  If the the value is freshly allocated
 * (meaning json_calc() is responsible for freeing it) then they also set
 * the "freeleft" and "freeright" variables.
 */
#define USE_LEFT_OPERAND(calc)	if ((left = jcsimple(calc->LEFT, context)) == NULL)\
		left = freeleft = json_calc(calc->LEFT, context, agdata)
#define USE_RIGHT_OPERAND(calc)	if ((right = jcsimple(calc->RIGHT, context)) == NULL)\
		right = freeright = json_calc(calc->RIGHT, context, agdata)

/* If calc is a literal or name, then we can retrieve the value without
 * allocating anything.  Do that, and return it.  Otherwise return NULL.
 */
static json_t *jcsimple(jsoncalc_t *calc, jsoncontext_t *context)
{
	json_t *tmp;

	/* If literal then return its value */
	if (calc->op == JSONOP_LITERAL)
		return calc->u.literal;

	/* If simple name then look it up */
	if (calc->op == JSONOP_NAME)
		return json_context_by_key(context, calc->u.text, NULL);

	/* We can do name.name too */
	if (calc->op == JSONOP_DOT
	 && calc->RIGHT->op == JSONOP_NAME
	 && (tmp = jcsimple(calc->LEFT, context)) != NULL
	 && tmp->type == JSON_OBJECT)
		return json_by_key(tmp, calc->RIGHT->u.text);

	/* We can choose a default table when SELECT is used without FROM */
	if (calc->op == JSONOP_FROM)
		return json_context_default_table(context, NULL);

	/* No joy */
	return NULL;
}


/* Implement @= natural join, @< left join, and @> right join */
json_t *jcnjoin(json_t *jl, json_t *jr, int left, int right)
{
	json_t  *scan, *result, *merge, *lmem, *rmem;
	int	leftmatch;
	char	*rightmatch, *r;

	/* We normally loop over the left argument in the outer loop, and the
	 * right argument in the inner loop.  If right is deferred and left
	 * isn't, then it's more efficient to use the right in the outer loop
	 * so switch the.
	 */
	if (!json_is_deferred_array(jl) && json_is_deferred_array(jr)) {
		/* Swap pointers */
		scan = jl;
		jl = jr;
		jr = scan;

		/* Swap left/right flags */
		leftmatch = left;
		left = right;
		right = leftmatch;
	}

	/* If we're doing right join, then we need a list of flags to
	 * keep track of which right elements never matched any left
	 */
	rightmatch = right ? (char *)calloc(json_length(jr), sizeof(char)) : NULL;

	/* Start with an empty result array */
	result = json_array();

	/* For each row from the left table... */
	for (jl = json_first(jl); jl; jl = json_next(jl)) {
		/* If interrupted, then discard any result so far and return
		 * an error null.
		 */
		if (json_interrupt) {
			json_free(result);
			json_break(jl);
			return json_error_null(NULL, "intr:Interrupted");
		}

		/* Skip if not an object */
		if (jl->type != JSON_OBJECT)
			continue;

		/* For each row from the right table... */
		leftmatch = 0;
		for (scan = json_first(jr), r = rightmatch; scan; scan = json_next(scan), r++) {
			/* Skip if not an object */
			if (scan->type != JSON_OBJECT)
				continue;

			/* If any members clash, skip this pairing */
			for (lmem = jl->first; lmem; lmem = lmem->next) { /* object */
				rmem = json_by_key(scan, lmem->text);
				if (rmem && !json_equal(lmem->first, rmem))
					break;
			}
			if (lmem)
				continue;

			/* Merge the objects */
			merge = json_copy(jl);
			for (rmem = scan->first; rmem; rmem = rmem->next) { /* object */
				lmem = json_by_key(merge, rmem->text);
				if (!lmem)
					json_append(merge, json_copy(rmem));
			}

			/* Add the merged object to the result */
			json_append(result, merge);

			/* Remember that there was a match */
			if (right)
				*r = 1;
			leftmatch = 1;
		}

		/* If doing a left join and left didn't match anything, add it
		 * by itself.
		 */
		if (left && !leftmatch)
			json_append(result, json_copy(jl));
	}

	/* If doing a right join, add any right elements that didn't match
	 * anything from the left.
	 */
	if (right) {
		for (scan = json_first(jr), r = rightmatch; scan; scan = json_next(scan), r++) {
			if (!*r)
				json_append(result, json_copy(scan));
		}
	}

	/* Return the result */
	return result;
}

/* Combine keys and values */
static json_t *jcvalues(json_t *keys, json_t *values)
{
	json_t	*key, *value, *vrow, *rowobj, *result;

	/* First argument must be an array of strings to use as keys */
	if (keys->type != JSON_ARRAY)
		key = keys; /* not NULL marking the end of happy scan */
	else {
		for (key = json_first(keys); key; key = json_next(key)) {
			if (key->type != JSON_STRING) {
				json_break(key);
				break;
			}
		}
	}
	if (key || !keys->first)
		return json_error_null(NULL, "valuesL:Left of VALUES must be an array of strings");

	/* Right argument may be either an array of values, or any array of
	 * arrays of values.  If all elements are arrays then assume the latter.
	 */
	if (values->type != JSON_ARRAY)
		return json_error_null(NULL, "valuesR:Right of VALUES must be array of values, or array of arrays of values");
	for (value = json_first(values); value; value = json_next(value)) {
		if (value->type != JSON_ARRAY) {
			/* Stop scanning the outer for-loop */
			json_break(value);

			/* Do the single object version */
			result = json_object();
			for (key = json_first(keys), value = json_first(values); key && value; key = json_next(key), value = json_next(value))
				json_append(result, json_key(key->text, json_copy(value)));

			/* If the number keys doesn't match number of values,
			 * then one of the first/next loops ended prematurely.
			 * Clean up!
			 */
			json_break(key);
			json_break(value);
			return result;
		}
	}

	/* We'll be doing the table version. */
	result = json_array();
	for (vrow = json_first(values); vrow; vrow = json_next(vrow)) {
		rowobj = json_object();
		for (key = json_first(keys), value = json_first(vrow);
		     key && value;
		     key = json_next(key), value = json_next(value))
			json_append(rowobj, json_key(key->text, json_copy(value)));
		json_break(key);
		json_break(value);
		json_append(result, rowobj);
	}
	return result;
}


/* Invoke all aggregates for the current item ("this" in context) */
static void jcag(jsonag_t *ag, jsoncontext_t *context, void *agdata)
{
	int     i;
	void    *fnag = agdata;
	json_t  *args;

	/* For each aggregate function... */
	for (i = 0; i < ag->nags; i++) {
		/* Evaluate its parameters */
		args = json_calc(ag->ag[i]->u.func.args, context, agdata);

		/* Aggregate functions can either accumulate data over rows
		 * of a table *OR* over an array passed as the first argument.
		 * Here we're accumulating, but if the first argument is an
		 * array then the accumulated result will be ignored so we
		 * might as well skip it.
		 */
		if (args->first->type != JSON_ARRAY) {
			/* Call the aggregator function */
			ag->ag[i]->u.func.jf->agfn(args, fnag);
		}

		/* Free its parameters */
		json_free(args);

		/* Find the location of the next function's storage */
		fnag = (void *)((char *)fnag + ag->ag[i]->u.func.jf->agsize);
	}
}

/* If calc uses aggregates, then allocate storage space for them and return
 * a pointer to that... or if existingag is non-NULL then reset it and return
 * it.  Otherwise return NULL to indicate that no aggregates are used.
 * Later, you can free the memory by calling json_calc_ag(NULL, ag) even if
 * ag is NULL.
 */
void *json_calc_ag(jsoncalc_t *calc, void *existingag)
{
	/* If passed an existingag, then we're either about to free it or
	 * reset it.  Either way, maybe some functions want us to free up
	 * some of allocated data for them.
	 */
	if (existingag) {
		/* There's a list of ag function calls before the data.  Get it.
		 *
		 */
		jsoncalc_t *ag = ((jsoncalc_t **)existingag)[-1];
		int	i;
		char	*data;
		void	*toFree;

		/* For each function call... */
		for (i = 0, data = (char *)existingag; i < ag->u.ag->nags; data += ag->u.ag->ag[i++]->u.func.jf->agsize) {
			jsonfunc_t *jf = ag->u.ag->ag[i]->u.func.jf;
			/* Supposed to free anything? */
			if (jf->jfoptions & JSONFUNC_JSONFREE) {
				json_t *doomed = *(json_t **)data;
				json_free(doomed);
				toFree = *(void **)(data + sizeof(json_t *));

			} else
				toFree = *(void **)data;
			if ((jf->jfoptions & JSONFUNC_FREE) && toFree) {
				free(toFree);
			}
		}
	}

	/* Passing NULL for calc just means we should free existingag, if any */
	if (!calc) {
		if (existingag)
			free((char*)existingag - sizeof(jsoncalc_t**));
		return NULL;
	}

	/* If no aggregates are used, then return NULL */
	if (calc->op != JSONOP_AG)
		return NULL;

	/* If no existingag, then allocate it now.  Also add space for a pointer */
	if (!existingag) {
		existingag = malloc(sizeof(jsoncalc_t **) + calc->u.ag->agsize) + sizeof(jsoncalc_t ***);
	}

	/* Reset it */
	memset(existingag, 0, calc->u.ag->agsize);
	((jsoncalc_t **)existingag)[-1] = calc;
	return existingag;
}
 
/* If a jsoncalc_t uses aggregate functions, then incorporate this row's
 * data into the aggregates.  If it doesn't use aggregates then do nothing.
 */
void json_calc_ag_row(jsoncalc_t *calc, jsoncontext_t *context, void *agdata, json_t *row)
{
	jsoncontext_t *local;

	/* If no aggregates, then do nothing. */
	if (calc->op != JSONOP_AG)
		return;

	/* We must have agdata if we're using aggregates. */
	assert(agdata != NULL);

	/* Create a context with this row's data in it, and evaluate all
	 * aggretators with that.
	 */
	local = json_context(context, row, JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);
	jcag(calc->u.ag, local, agdata);
	json_context_free(local);
}

/* This implements the @ and @@ operators.  "arr" is normally an array of items
 * to loop over, but it can also be a single item to treat as a singleton array.
 * "expr" is an expression to apply to each member of the array (which may
 * include aggregate functions), and op is either JSONOP_EACH or JSONOP_GROUP.
 */
json_t *jceach(json_t *arr, jsoncalc_t *calc, jsoncontext_t *context, jsonop_t op)
{
	json_t	*scan, *gscan;
	json_t	*result, *tmp;
	jsoncontext_t *local;
	void *ag, **groupag;
	int	ngroups, nongroup, g;

	/* The array may include subarrays to indicate grouping. If grouping
	 * is used, there may or may not be ungrouped items.  We'll need
	 * separate aggregate data for each group, and one for the ungrouped
	 * aggregates.  STEP ONE: Allocate overall ag data, as a way to detect
	 * whether aggregates are indeed used.
	 */
	ngroups = 0;
	groupag = NULL;
	ag = json_calc_ag(calc, NULL);
	if (ag) {
		/* STEP 1: Count groups, and watch for any ungrouped elements */
		for (ngroups = nongroup = 0, scan = json_first(arr); scan; scan = json_next(scan)) {
			if (scan->type == JSON_ARRAY)
				ngroups++;
			else
				nongroup++;
		}

		/* STEP 2: Allocate an array to hold groups' aggregate data */
		if (ngroups > 0) {
			groupag = calloc(ngroups, sizeof(void *));
			for (g = 0; g < ngroups; g++)
				groupag[g] = json_calc_ag(calc, NULL);
		}

		/* STEP 3: Loop over the array to generate aggregate data. */
		for (g = 0, scan = json_first(arr); scan; scan = json_next(scan)) {
			if (json_interrupt) {
				json_break(scan);
				return json_error_null(NULL, "intr:Interrupted");
			}

			/* Is this element a nested array? */
			if (scan->type == JSON_ARRAY) {
				/* Loop over the array elements */
				for (gscan = json_first(scan); gscan; gscan = json_next(gscan)) {
					if (json_interrupt) {
						json_break(gscan);
						json_break(scan);
						return json_error_null(NULL, "intr:Interrupted");
					}

					/* Invoke the aggregators on "this" */
					local = json_context(context, gscan, JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);
					jcag(calc->u.ag, local, groupag[g]);
					if (nongroup)
						jcag(calc->u.ag, local, ag);
					json_context_free(local);
				}

				/* Prepare for next group */
				g++;
			} else {
				/* Invoke the aggregators on "this" */
				local = json_context(context, scan, JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);
				jcag(calc->u.ag, local, ag);
				json_context_free(local);
			}
		}
	}

	/* Loop over the array.  For each element, make it "this" and
	 * evaluate the right operand.  Collect the results in a new
	 * array.
	 */
	result = json_array();
	for (g = 0, scan = json_first(arr); scan; scan = json_next(scan)) {
		/* Is it a group (nested array) ? */
		if (scan->type == JSON_ARRAY) {
			/* Process the group using the group's own aggregate
			 * data.  For EACH process all of them, for GROUP only
			 * process the first.
			 */
			for (gscan = json_first(scan); gscan; gscan = (op == JSONOP_EACH ? json_next(gscan) : NULL)) {
				/* If interrupted then discard results so far
				 * and return an error null.
				 */
				if (json_interrupt) {
					json_free(result);
					json_break(gscan);
					json_break(scan);
					return json_error_null(NULL, "intr:Interrupted");
				}

				/* Evaluate with element as "this" */
				local = json_context(context, gscan, JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);
				tmp = json_calc(calc, local, ag ? groupag[g] : NULL);
				json_context_free(local);

				/* If null/false, skip it, if true add element*/
				if (tmp->type == JSON_NULL || tmp->type == JSON_BOOLEAN) {
					/* Skip for null or false, add for true */
					if (json_is_true(tmp))
						json_append(result, json_copy(gscan));
					json_free(tmp);
				} else {
					/* Not a symbol, append whatever it is */
					json_append(result, tmp);
				}
			}
			json_break(gscan); /* since JSONOP_GROUP stops early */

			/* Prepare for the next group */
			g++;
		} else {
			/* If interrupted then discard results so far
			 * and return an error null.
			 */
			if (json_interrupt) {
				json_free(result);
				return json_error_null(NULL, "intr:Interrupted");
			}

			local = json_context(context, scan, JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);
			tmp = json_calc(calc, local, ag);
			json_context_free(local);
			if (tmp->type == JSON_NULL || tmp->type == JSON_BOOLEAN) {
				/* Skip for null or false, add for true */
				if (json_is_true(tmp))
					json_append(result, json_copy(scan));
				json_free(tmp);
			} else {
				/* Not a symbol, append whatever it is */
				json_append(result, tmp);
			}
		}
	}

	/* Clean up */
	json_calc_ag(NULL, ag);
	if (ngroups > 0) {
		for (g = 0; g < ngroups; g++)
			json_calc_ag(NULL, groupag[g]);
		free(groupag);
	}

	/* Done! */
	return result;
}


/* Evaluate an expression and return the result.
 *   calc       The expression to evaluate.  This should be obtained from a 
 *              previous call to json_calc_parse().
 *   context    A list of objects providing context for the expression.
 *              The first element is "this".  Any element that's an object
 *              can be scanned to obtain variable names.  May be NULL.
 *   agdata     Storage space for aggregate functions, allocated by
 *              json_calc_ag(calc, NULL), freed by json_calc_ag(NULL, ag);
 *
 * NOTE: For runtime errors, this mostly returns a "null" json_t node
 * containing an error message in ->text, and the error code in ->first.
 */
json_t *json_calc(jsoncalc_t *calc, jsoncontext_t *context, void *agdata)
{
	json_t *left, *right, *freeleft, *freeright;
	json_t *result;
	jsoncalc_t *tmp;
	json_t  *scan, *found;
	double  nl, nr;
	int     il,ir;
	char    *str;
	void    *localag;
	jsonfuncextra_t recon;

	/* If interrupted then simply return an error null */
	if (json_interrupt)
		return json_error_null(NULL, "intr:Interrupted");

	/* Start with freeleft and freeleft set to NULL.  The USE_LEFT_OPERAND
	 * and USE_RIGHT_OPERAND macros will set them if appropriate.
	 */
	freeleft = freeright = result = NULL;

	/* Process the expression */
	switch (calc->op)
	{
	  case JSONOP_LITERAL:
		result = json_copy(calc->u.literal);
		break;

	  case JSONOP_NAME:
		result = json_copy(json_context_by_key(context, calc->u.text, NULL));
		break;

	  case JSONOP_ENVIRON:
		/* Either $name or $name[subscr].  calc->LEFT is always a
		 * JSONOP_NAME, and calc->RIGHT is NULL or subscript expression.
		 */
		assert(calc->LEFT->op == JSONOP_NAME);
		if (calc->RIGHT) {
			char	*name, *sub;
			USE_RIGHT_OPERAND(calc);
			if (right->type == JSON_STRING
			 || right->type == JSON_BOOLEAN
			 || (right->type == JSON_NUMBER && right->text[0])) {
				name = (char *)malloc(strlen(calc->LEFT->u.text) + strlen(right->text) + 1);
				strcpy(name, calc->LEFT->u.text);
				strcat(name, right->text);
				str = getenv(name);
				free(name);
			} else {
				sub = json_serialize(right, NULL);
				name = (char *)malloc(strlen(calc->LEFT->u.text) + strlen(sub) + 1);
				strcpy(name, calc->LEFT->u.text);
				strcat(name, sub);
				str = getenv(name);
				free(name);
				free(sub);
			}
		} else {
			str = getenv(calc->LEFT->u.text);
		}

		/* If we found a value, then convert it to a json_t;
		 * otherwise return a null.
		 */
		if (str)
			result = json_string(str, -1);
		else
			result = json_null();
		str = NULL;
		break;

	  case JSONOP_ARRAY:
		/* Append the value of each element into an array. */
		result = json_array();
		if (calc->LEFT)
			json_append(result, json_calc(calc->LEFT, context, agdata));
		for (tmp = calc->RIGHT; tmp; tmp = tmp->RIGHT)
			json_append(result, json_calc(tmp->LEFT, context, agdata));
		break;

	  case JSONOP_OBJECT:
		/* Append name:value pairs into an object */
		result = json_object();
		if (calc->LEFT) {
			/* calc->LEFT is the first name:value.
			 * tmp is the name, with op=JSONOP_NAME.
			 * right is the value, to evaluate via json_calc()
			 */
			tmp = calc->LEFT->LEFT;
			found = json_calc(calc->LEFT->RIGHT, context, agdata);
			if (calc->LEFT->op != JSONOP_MAYBEMEMBER || !json_is_null(found))
				json_append(result, json_key(tmp->u.text, found));
			else
				json_free(found);
		}
		for (calc = calc->RIGHT; calc; calc = calc->RIGHT) {
			/* calc->LEFT is the next name:value.
			 * tmp is the name, with op=JSONOP_NAME.
			 * right is the value, to evaluate via json_calc()
			 */
			tmp = calc->LEFT->LEFT;
			found = json_calc(calc->LEFT->RIGHT, context, agdata);
			if (calc->LEFT->op != JSONOP_MAYBEMEMBER || !json_is_null(found))
				json_append(result, json_key(tmp->u.text, found));
			else
				json_free(found);
		}
		return result;

	  case JSONOP_SUBSCRIPT:
		USE_LEFT_OPERAND(calc);
		if (calc->RIGHT->op == JSONOP_COLON) {
			/* Subscript by name:value, scans an array of objects
			 * for a given member and value.
			 */
			if (left->type != JSON_ARRAY)
				break;

			/* Evaluate the value of name:value.  Also fetch name */
			USE_RIGHT_OPERAND(calc->RIGHT);
			str = calc->RIGHT->LEFT->u.text;

			/* Scan array for element with that member name:value */
			for (scan = json_first(left); scan; scan = json_next(scan)) {
				if (scan->type != JSON_OBJECT)
					continue;
				found = json_by_key(scan, str);
				if (found && json_equal(found, right)) {
					result = json_copy(scan);
					json_break(scan);
					break;
				}
			}
			break;
		} else {
			/* Evaluate the subscript.  Strings only work for
			 * objects, numbers only work for arrays or strings.
			 */
			USE_RIGHT_OPERAND(calc);
			if (left->type == JSON_OBJECT && right->type == JSON_STRING)
				result = json_by_key(left, right->text);
			else if (left->type == JSON_OBJECT) {
				/* convert to a string, use it as the key */
				str = json_serialize(right, NULL);
				result = json_by_key(left, str);
				free(str);
			} else if (left->type == JSON_ARRAY && right->type == JSON_NUMBER)
				result = json_by_index(left, json_int(right));
			else if (left->type == JSON_STRING && right->type == JSON_NUMBER) {
				size_t len = strlen(left->text);
				size_t end = 1; /* single character */
				ir = json_int(right);
				if (ir < 0)
					ir += len;
				if (ir >= 0 && ir < len) {
					const char *str = json_mbs_substr(left->text, ir, &end);
					result = json_string(str, end);
					break;
				}
			}
		}

		/* Use a copy of the result.  Also, call json_break() on it,
		 * just in case it came from a deferred array.
		 */
		found = json_copy(result);
		json_break(result);
		result = found;
		break;

	  case JSONOP_FNCALL:
		/* Collect parameter values into an array */
		freeleft = left = json_calc(calc->u.func.args, context, agdata);

		/* Aggregate functions are special, if the first parameter is
		 * an array.  (The parser can't always tell whether the first
		 * parameter is going to be an array, so it'll create a
		 * JSONOP_AG node above this which may result it data being
		 * accumulated that way.  But if passed an array, it'll ignore
		 * that aggregated data and create new aggregated data from
		 * the array.)
		 */
		if (left->first->type == JSON_ARRAY && calc->u.func.jf->agfn) {
			jsonfunc_t *jf = calc->u.func.jf;
			void **toFree;

			/* Allocate storage for the function */
			localag = malloc(jf->agsize);
			memset(localag, 0, jf->agsize);

			/* For each element of the array, create a new parameter
			 * list and call the aggregator.  Note that we don't
			 * need to create a new context, because all parameters
			 * have already been calculated.
			 */
			found = json_array();
			for (scan = json_first(left->first); scan; scan = json_next(scan)) { /* undeferred */
				/* Create a new argument list.  The first is an
				 * element from the array, and any other args
				 * are used unchanged. To accomplish this, we
				 * will temporarily mangle scan's "next".
				 */
				json_t *scannext = scan->next; /* undeferred */
				scan->next = left->first->next; /* undeferred */
				found->first = scan;

				/* Invoke the aggregator */
				(*jf->agfn)(found, localag);

				/* Restore scan's "next" pointer */
				scan->next = scannext; /* undeferred */
			}
			found->first = NULL;
			json_free(found);

			/* Invoke the function */
			result = (*jf->fn)(left, localag);

			/* Clean up */
			if (jf->jfoptions & JSONFUNC_JSONFREE) {
				json_free(*(json_t **)localag);
				toFree = (void **)((json_t **)localag + 1);
			} else
				toFree = (void **)localag;
			if (jf->jfoptions & JSONFUNC_FREE && *toFree != NULL)
				free(*toFree);
			free(localag);
		} else {
			/* Non-aggregate built-in functions may take a regular
			 * expression.  Since that isn't a JSON data type,
			 * the args list will just contain "null" there; we
			 * need to scan the argument array generator for a
			 * JSONOP_REGEX... but only for non-aggregate built-ins.
			 */
			localag = (void *)((char *)agdata + calc->u.func.agoffset);
			if (!calc->u.func.jf->agfn && !calc->u.func.jf->user) {
				recon.context = context;
				recon.regex = NULL;
				if (!calc->u.func.jf->user) {
					tmp = calc->u.func.args;
					if (tmp->LEFT && tmp->LEFT->op == JSONOP_REGEX)
						tmp = tmp->LEFT;
					else for (tmp = tmp->RIGHT; tmp; tmp = tmp->RIGHT)
						if (tmp->LEFT->op == JSONOP_REGEX) {
							tmp = tmp->LEFT;
							break;
					}
					recon.regex = (void *)tmp;
				}
				localag = (void *)&recon;
			}

			/* Invoke the function. For built-ins, call the
			 * function directly ("jf->fn").  For user-defined
			 * functions, call json_cmd_fncall() to do it.
			 */
			if (calc->u.func.jf->user)
				result = json_cmd_fncall(left, calc->u.func.jf, context);
			else if (calc->u.func.jf->fn)
				result = (*calc->u.func.jf->fn)(left, localag);
			else
				result = NULL; /* probably an empty user func */
		}
		break;

	  case JSONOP_AG:
		/* We always expect agdata when we're using aggregates, but
		 * if we aren't given agdata then use blank agdata.  We won't
		 * get useful results that way, but at least we won't dump core.
		 */
		if (!agdata) {
			/* Evaluate using blank agdata */
			localag = json_calc_ag(calc, NULL);
			result = json_calc(calc->u.ag->expr, context, localag);
			localag = json_calc_ag(NULL, localag);
		} else {
			/* Evaluate expr, but use *this* agdata to do it */
			result = json_calc(calc->u.ag->expr, context, agdata);
		}
		break;

	  case JSONOP_FIND:
		/* Evaluate the left operand.  Then pass that result and the
		 * right operand to json_find_calc() to build the result table.
		 */
		USE_LEFT_OPERAND(calc);
		if (json_is_null(left))
			result = left;
		else
			result = json_find_calc(left, calc->RIGHT, context);
		break;

	  case JSONOP_EACH:
	  case JSONOP_GROUP:
		/* Evaluate the left operand.  If null then return an empty
		 * array.  If it is an array then set scan to its first
		 * element; if not an array then set scan to it directly,
		 * so it'll effectively be treated like a single-element array.
		 */
		USE_LEFT_OPERAND(calc);
		if (json_is_null(left)) {
			result = json_array();
			break;
		}

		/* Do the thing */
		result = jceach(left, calc->RIGHT, context, calc->op);
		break;

	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
		/* Natural join of left and right arrays.  The pairing-up
		 * logic is implemented in jcnjoin(), but we still have a bit
		 * of operand evaluation and cleanup to worry about here.
		 */
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		result = jcnjoin(left, right, calc->op == JSONOP_LJOIN, calc->op == JSONOP_RJOIN);
		/* NOTE: jcnjoin() always copies any data it uses.  Nothing
		 * in it could still be used by jl or jr.
		 */
		break;

	  case JSONOP_DOT:
		/* NOTE: Function calls of the form data.func(args...) are
		 * transformed to func(data, args...) during parsing, so we
		 * only see the . operator while looking for a member of an
		 * object.
		 */
		assert(calc->RIGHT->op == JSONOP_NAME);
		USE_LEFT_OPERAND(calc);
		if (left->type == JSON_OBJECT && (result = json_by_key(left, calc->RIGHT->u.text)) != NULL)
			result = json_copy(result);
		else if (!strcasecmp(calc->RIGHT->u.text, "length")) {
			/* The "length" attribute is computed, for strings and
			 * arrays.  To simplify processing of data that was
			 * converted from XML, we also return 0 for null.length
			 * and 1 for anything_else.length -- XML doesn't do
			 * arrays very well.
			 */
			if (left->type == JSON_ARRAY)
				result = json_from_int(json_length(left));
			else if (left->type == JSON_STRING)
				result = json_from_int(json_mbs_len(left->text));
			else if (json_is_null(left))
				result = json_from_int(0);
			else
				result = json_from_int(1);
		}
		break;

	  case JSONOP_DOTDOT:
		USE_LEFT_OPERAND(calc);
		if (left->type != JSON_OBJECT && calc->RIGHT->op == JSONOP_NAME)
			result = json_copy(json_by_deep_key(left, calc->RIGHT->u.text));
		break;

	  case JSONOP_ELLIPSIS:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			il = json_int(left);
			ir = json_int(right);
#if 0
			result = json_array();
			for (; il <= ir; il++) {
				json_append(result, json_from_int(il));
			}
#else
			result = json_defer_ellipsis(il, ir);
#endif
		}
		break;

	  case JSONOP_COALESCE:
		/* If left arg is non-null, return it */
		USE_LEFT_OPERAND(calc);
		if (!json_is_null(left)) {
			if (freeleft) {
				result = left;
				freeleft = NULL;
			} else
				result = json_copy(left);
			break;
		}

		/* Else return right arg */
		USE_RIGHT_OPERAND(calc);
		if (freeright) {
			result = right;
			freeright = NULL;
		} else
			result = json_copy(right);
		break;

	  case JSONOP_QUESTION:
		USE_LEFT_OPERAND(calc);
		/* Can be test?then or test?them:else */
		if (calc->RIGHT->op == JSONOP_COLON) {
			if (json_is_true(left))
				result = json_calc(calc->RIGHT->LEFT, context, agdata);
			else
				result = json_calc(calc->RIGHT->RIGHT, context, agdata);
		} else {
			if (json_is_true(left)) {
				USE_RIGHT_OPERAND(calc);
				if (freeright) {
					result = freeright;
					freeright = NULL;
				} else {
					result = json_copy(right);
				}
			}
		}
		break;

	  case JSONOP_COLON:
		/* Shouldn't happen. */
		abort();

	  case JSONOP_ISNULL:
		USE_RIGHT_OPERAND(calc);
		result = json_boolean(json_is_null(right));
		break;

	  case JSONOP_ISNOTNULL:
		USE_RIGHT_OPERAND(calc);
		result = json_boolean(!json_is_null(right));
		break;

	  case JSONOP_NEGATE:
		USE_RIGHT_OPERAND(calc);
		if (right->type == JSON_NUMBER || right->type == JSON_STRING)
			result = json_from_double(-json_double(right));
		break;

	  case JSONOP_ADD:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (json_is_date(left) && json_is_period(right)) {
			/* ISO datetime+period.  Add the period to the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			json_datetime_add(buf, left->text, right->text);
			buf[10] = '\0';
			result = json_string(buf, -1);
		} else if ((json_is_date(left) || json_is_datetime(left)) && json_is_period(right)) {
			/* ISO datetime+period.  Add the period to the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			json_datetime_add(buf, left->text, right->text);
			result = json_string(buf, -1);
		} else if (left->type == JSON_STRING || right->type == JSON_STRING) {
			/* String version.  If one of the operands is a
			 * non-string, then convert it to a string.
			 * One minor optimization is that if a number is in
			 * text form, or a boolean, then we can treat it as
			 * a string already.
			 */
			if ((left->type == JSON_STRING || left->type == JSON_BOOLEAN || (left->type == JSON_NUMBER && *left->text))
			 && (right->type == JSON_STRING || right->type == JSON_BOOLEAN || (right->type == JSON_NUMBER && *right->text))) {
				/* Both are strings, or at least stringy */
				result = json_string(left->text, strlen(left->text) + strlen(right->text));
				strcat(result->text, right->text);
			} else if (left->type == JSON_NULL) {
				if (freeright) {
					result = right;
					freeright = NULL;
				} else
					result = json_copy(right);
			} else if (right->type == JSON_NULL) {
				if (freeleft) {
					result = left;
					freeleft = NULL;
				} else
					result = json_copy(left);
			} else if (left->type != JSON_STRING) {
				/* Left operand needs to be converted */
				str = json_serialize(left, NULL);
				result = json_string(str, strlen(str) + strlen(right->text));
				strcat(result->text, right->text);
				free(str);
			} else { /* Right is not stringy */
				/* Right operand needs to be converted */
				str = json_serialize(right, NULL);
				result = json_string(left->text, strlen(left->text) + strlen(str));
				strcat(result->text, str);
				free(str);
			}
		}
		else if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			/* Number version */
			result = json_from_double(json_double(left) + json_double(right));
		}
		break;

	  case JSONOP_SUBTRACT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (json_is_date(left) && json_is_period(right)) {
			/* ISO date-period.  Subtract the period from the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			json_datetime_subtract(buf, left->text, right->text);
			buf[10] = '\0';
			result = json_string(buf, -1);
		} else if (json_is_datetime(left) && json_is_period(right)) {
			/* ISO datetime-period.  Subtract the period from the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			json_datetime_subtract(buf, left->text, right->text);
			result = json_string(buf, -1);
		} else if ((json_is_date(left) || json_is_datetime(left))
		        && (json_is_date(right) || json_is_datetime(right))) {
			/* ISO datetime-datetime.  Find the difference between
			 * two dates, and return it as a period.
			 * Note that the datetimes are strings.
			 */
			char buf[50];
			json_datetime_diff(buf, left->text, right->text);
			result = json_string(buf, -1);
		} else if (left->type == JSON_STRING || right->type == JSON_STRING) {
			/* String version.  If one of the operands is a
			 * non-string, then convert it to a string.
			 * One minor optimization is that if a number is in
			 * text form, or a boolean, then we can treat it as
			 * a string already.
			 */
			char	*leftstr, *rightstr;
			size_t	leftlen;
			str = NULL;
			if ((left->type == JSON_STRING || left->type == JSON_BOOLEAN || (left->type == JSON_NUMBER && *left->text))
			 && (right->type == JSON_STRING || right->type == JSON_BOOLEAN || (right->type == JSON_NUMBER && *right->text))) {
				/* Both are strings, or at least stringy */
				leftstr = left->text;
				rightstr = right->text;
			} else if (left->type == JSON_NULL) {
				if (freeright) {
					result = right;
					freeright = NULL;
				} else
					result = json_copy(right);
				break;
			} else if (right->type == JSON_NULL) {
				if (freeleft) {
					result = left;
					freeleft = NULL;
				} else
					result = json_copy(left);
				break;
			} else if (left->type != JSON_STRING) {
				/* Left operand needs to be converted */
				str = json_serialize(left, NULL);
				leftstr = str;
				rightstr = right->text;
			} else { /* Right is not stringy */
				/* Right operand needs to be converted */
				str = json_serialize(right, NULL);
				leftstr = left->text;
				rightstr = str;
			}

			/* Trim trailing spaces from leftstr and leading spaces
			 * from rightstr.
			 */
			for (leftlen = strlen(leftstr); leftlen > 0 && leftstr[leftlen - 1] == ' '; leftlen--) {
			}
			while (*rightstr == ' ')
				rightstr++;

			/* Allocate a response big enough for both trimmed
			 * strings and a space between them.  Copy text.
			 */
			result = json_string(leftstr, leftlen + 1 + strlen(rightstr));
			if (leftlen > 0 && *rightstr)
				result->text[leftlen++] = ' ';
			strcpy(result->text + leftlen, rightstr);

			/* If we had to serialize a value, free that now */
			if (str)
				free(str);
		}
		else if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			/* Number version */
			result = json_from_double(json_double(left) - json_double(right));
		}
		break;

	  case JSONOP_MULTIPLY:
	  case JSONOP_DIVIDE:
	  case JSONOP_MODULO:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			/* Convert to binary */
			nl = json_double(left);
			nr = json_double(right);

			/* Do the math */
			if (calc->op == JSONOP_MULTIPLY)
				result = json_from_double(nl * nr);
			else if (nr == 0.0)
				result = json_error_null(NULL, "div0:division by 0");
			else if (calc->op == JSONOP_DIVIDE)
				result = json_from_double(nl / nr);
			else if ((int)nr == 0)
				result = json_error_null(NULL, "mod0:modulo by 0");
			else /* JSONOP_MODULO */
				result = json_from_double((int)nl % (int)nr);
		}
		break;

	  case JSONOP_BITNOT:
		USE_RIGHT_OPERAND(calc);
		if (right->type == JSON_NUMBER)
			result = json_from_int(~json_int(right));
		break;

	  case JSONOP_BITAND:
	  case JSONOP_BITOR:
	  case JSONOP_BITXOR:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			/* Convert to binary */
			il = json_int(left);
			ir = json_int(right);

			/* Do the bitwise math */
			if (calc->op == JSONOP_BITAND)
				result = json_from_int(il & ir);
			else if (calc->op == JSONOP_BITOR)
				result = json_from_int(il | ir);
			else /* JSONOP_BITOR */
				result = json_from_int(il ^ ir);
		} else if (left->type == JSON_OBJECT && right->type == JSON_OBJECT) {
			if (calc->op == JSONOP_BITAND) {
				/* Keep left keys/values only if same key is in right */
				result = json_object();
				for (scan = left->first; scan; scan = scan->next) { /* object */
					if (json_by_key(right, scan->text))
						json_append(result, json_copy(scan));
				}
			} else if (calc->op == JSONOP_BITOR) {
				/* Merge right keys/values into left */
				result = json_copy(left);
				for (scan = right->first; scan; scan = scan->next) { /* object */
					json_append(result, json_copy(scan));
				}
			} else { /* JSONOP_BITOR */
				/* Keep left keys/values only if key is NOT in right */
				result = json_object();
				for (scan = left->first; scan; scan = scan->next) { /* object */
					if (!json_by_key(right, scan->text))
						json_append(result, json_copy(scan));
				}
			}
		}
		break;

	  case JSONOP_NOT:
		USE_RIGHT_OPERAND(calc);
		result = json_boolean(!json_is_true(right));
		break;

	  case JSONOP_AND:
	  case JSONOP_OR:
		USE_LEFT_OPERAND(calc);
		il = json_is_true(left);
		if (calc->op == (il ? JSONOP_AND : JSONOP_OR)) {
			USE_RIGHT_OPERAND(calc);
			il = json_is_true(right);
		}
		result = json_boolean(il);
		break;

	  case JSONOP_EQSTRICT:
	  case JSONOP_NESTRICT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Compare them using json_equal(), which checks data types.
		 * It also does a "deep" comparison, allowing you to compare
		 * the contents of arrays, or of objects.
		 */
		il = json_equal(left, right);
		if (calc->op == JSONOP_NESTRICT)
			il = !il;
		result = json_boolean(il);
		break;

	  case JSONOP_LT:
	  case JSONOP_LE:
	  case JSONOP_EQ:
	  case JSONOP_NE:
	  case JSONOP_GE:
	  case JSONOP_GT:
	  case JSONOP_ICEQ:
	  case JSONOP_ICNE:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Arrays and objects can't be compared this way.  They can
		 * be compared for strict equality, but not this.
		 */
		if (left->type == JSON_ARRAY || left->type == JSON_OBJECT
		 || right->type == JSON_ARRAY || right->type == JSON_OBJECT) {
			result = json_error_null(NULL, "cmpObjArr:Can't compare objects/arrays except via === or !==");
			break;
		}

		/* Compare them in an appropriate way */
		if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			nl = json_double(left);
			nr = json_double(right);
			if (nl < nr)
				il = -1;
			else if (nl > nr)
				il = 1;
			else
				il = 0;
		} else if ((left->type == JSON_BOOLEAN || right->type == JSON_BOOLEAN)
		        && (calc->op == JSONOP_EQ || calc->op == JSONOP_NE)) {
			/* Compare as booleans, but only for equality */
			il = json_is_true(left) != json_is_true(right);
		} else if (left->type == JSON_NULL || right->type == JSON_NULL){
		        /* We allow equality comparisons to null. Anything else
		         * is always false.
		         */
		        if (calc->op == JSONOP_EQ || calc->op == JSONOP_NE)
				il = (left->type != right->type);
			else {
				result = json_boolean(0);
				break;
			}
		} else if ((left->type == JSON_NUMBER && right->type == JSON_STRING)
			|| (left->type == JSON_STRING && right->type == JSON_NUMBER)) {
			/* When comparing strings and numbers, convert the
			 * string to a number.
			 */
			if (left->type == JSON_NUMBER)
				nl = json_double(left);
			else {
				nl = strtod(left->text, &str);
				if (*str) {
					/* Not a clean conversion, so not equal */
					result = json_boolean(calc->op == JSONOP_NE || calc->op == JSONOP_ICNE);
					break;
				}
			}
			if (right->type == JSON_NUMBER)
				nr = json_double(right);
			else {
				nr = strtod(right->text, &str);
				if (*str) {
					/* Not a clean conversion, so not equal */
					result = json_boolean(calc->op == JSONOP_NE || calc->op == JSONOP_ICNE);
					break;
				}
			}
			if (nl < nr)
				il = -1;
			else if (nl > nr)
				il = 1;
			else
				il = 0;
		} else {/* hopefully string, but other types work too */
			if (calc->op == JSONOP_ICEQ || calc->op == JSONOP_ICNE){
				size_t lenl, lenr, spacesl, spacesr;

				/* The tricky thing here is that we want to
				 * ignore trailing spaces.  This sounds like
				 * a job for json_mbs_ncasecmp(), but that
				 * function specifies length by character
				 * count, but trailing spaces are easier to
				 * find via byte count, so we kind of have to
				 * do it both ways.
				 */

				/* Find the string lengths, in bytes */
				lenl = strlen(left->text);
				lenr = strlen(right->text);

				/* Count trailing spaces */
				for (spacesl = 0; spacesl < lenl && left->text[lenl - spacesl - 1] == ' '; spacesl++) {
				}
				for (spacesr = 0; spacesr < lenr && right->text[lenr - spacesr - 1] == ' '; spacesr++) {
				}

				/* Now we switch from bytes to characters.
				 * For the spacesl and spacesr variables,
				 * no conversion is needed since spaces are
				 * 1 byte each, always.  But lenl and lenr
				 * need to be recounted.
				 */
				lenl = json_mbs_len(left->text);
				lenr = json_mbs_len(right->text);

				/* Compare trimmed lengths.  If not the same
				 * then the strings don't match.  Otherwise we
				 * need to check the characters.
				 */
				if (lenl - spacesl != lenr - spacesr)
					il = 1;	/* trimmed lengths differ */
				else if (lenl - spacesl == 0)
					il = 0; /* both are empty */
				else
					il = json_mbs_ncasecmp(left->text, right->text, lenl - spacesl);
			} else
				il = strcmp(left->text, right->text);
		}

		/* Choose a comparison */
		switch (calc->op) {
		  case JSONOP_EQ:
		  case JSONOP_ICEQ: ir = (il == 0); break;
		  case JSONOP_NE:
		  case JSONOP_ICNE: ir = (il != 0); break;
		  case JSONOP_LT:   ir = (il < 0);  break;
		  case JSONOP_LE:   ir = (il <= 0); break;
		  case JSONOP_GE:   ir = (il >= 0); break;
		  default: /* GT */ ir = (il > 0);  break;
		}

		/* Set the result */
		result = json_boolean(ir);
		break;

	  case JSONOP_BETWEEN:
		assert(calc->RIGHT->op == JSONOP_AND);
		USE_LEFT_OPERAND(calc);

		/* Test lower bound. Note that we have to use USE_LEFT_OPERAND()
		 * again since calc->RIGHT is the entire "AND" clause and we
		 * want the left branch of that.  So first we juggle variables
		 * a bit...
		 */
		scan = left;
		found = freeleft;
		USE_LEFT_OPERAND(calc->RIGHT);
		right = left;
		freeright = freeleft;
		left = scan;
		freeleft = found;
		if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
			if (json_double(left) < json_double(right))
				result = json_boolean(0);
		} else if (left->type == JSON_STRING && right->type == JSON_STRING) {
			if (json_mbs_casecmp(left->text, right->text) < 0)
				result = json_boolean(0);
		} else if ((left->type == JSON_NUMBER && right->type == JSON_STRING)
			|| (left->type == JSON_STRING && right->type == JSON_NUMBER)) {
			/* When comparing strings and numbers, convert the
			 * string to a number.
			 */
			if (left->type == JSON_NUMBER)
				nl = json_double(left);
			else {
				nl = strtod(left->text, &str);
				if (*str) {
					/* Not a clean conversion */
					result = json_boolean(0);
					break;
				}
			}
			if (right->type == JSON_NUMBER)
				nr = json_double(right);
			else {
				nr = strtod(right->text, &str);
				if (*str) {
					/* Not a clean conversion */
					result = json_boolean(0);
					break;
				}
			}
			if (nl < nr)
				result = json_boolean(0);
		} else
			result = json_error_null(NULL, "between:BETWEEN only works on strings and numbers");

		if (freeright) {
			json_free(freeright);
			freeright = NULL;
		}

		/* Test upper bound.  If we already know the tested value is
		 * below the lower bound, we can skip this.
		 */
		if (!result) {
			USE_RIGHT_OPERAND(calc->RIGHT);
			if (left->type == JSON_NUMBER && right->type == JSON_NUMBER) {
				if (json_double(left) > json_double(right))
					result = json_boolean(0);
			} else if (left->type == JSON_STRING && right->type == JSON_NUMBER) {
				if (json_mbs_casecmp(left->text, right->text) > 0)
					result = json_boolean(0);
			} else if ((left->type == JSON_NUMBER && right->type == JSON_STRING)
				|| (left->type == JSON_STRING && right->type == JSON_NUMBER)) {
				/* When comparing strings and numbers, convert
				 * the string to a number.
				 */
				if (left->type == JSON_NUMBER)
					nl = json_double(left);
				else {
					nl = strtod(left->text, &str);
					if (*str) {
						/* Not a clean conversion */
						result = json_boolean(0);
						break;
					}
				}
				if (right->type == JSON_NUMBER)
					nr = json_double(right);
				else
					nr = strtod(right->text, &str);
				if (nl > nr) {
					result = json_boolean(0);
					if (*str) {
						/* Not a clean conversion */
						result = json_boolean(0);
						break;
					}
				}
			} else
				result = json_error_null(NULL, "between:BETWEEN only works on strings and numbers");
		}

		/* If no result, I guess we're okay */
		if (!result)
			result = json_boolean(1);

		break;

	  case JSONOP_LIKE:
	  case JSONOP_NOTLIKE:
		USE_LEFT_OPERAND(calc);
		if (calc->RIGHT->op == JSONOP_REGEX) {
			regmatch_t matches[10];
			if (left->type == JSON_STRING
			 && regexec((regex_t *)calc->RIGHT->u.regex.preg, left->text, 10, matches, 0) == 0)
				result = json_boolean(1);
			else
				result = json_boolean(0);
		} else  {
			USE_RIGHT_OPERAND(calc);
			if (left->type != JSON_STRING || right->type != JSON_STRING) {
				result = json_boolean(0);
			} else {
				il = json_mbs_like(left->text, right->text);
				if (calc->op == JSONOP_NOTLIKE)
					il = !il;
				result = json_boolean(il);
			}
		}
		break;

	  case JSONOP_IN:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Scan the right-hand list, looking for an exact match */
		if (right->type == JSON_ARRAY) {

			if (left->type == JSON_STRING) {
				/* Is "right" a single-column table? */
				if (json_is_table(right)) {
					/* If single column, check value */
					for (scan = json_first(right); scan; scan = json_next(scan)) {
						if (scan->first->first
						 && scan->first->first->type == JSON_STRING
						 && scan->first->next == NULL /* object */
						 && !json_mbs_casecmp(left->text, scan->first->first->text))
							break;
					}
				} else {
					/* compare strings to strings */
					for (scan = json_first(right); scan; scan = json_next(scan)) {
						if (scan->type == JSON_STRING
						 && !json_mbs_casecmp(left->text, scan->text))
							break;
					}
				}

			} else if (left->type == JSON_NUMBER && json_is_table(right)) {
				/* If single column, compare to value */
				for (scan = json_first(right); scan; scan = json_next(scan)) {
					if (scan->first->next == NULL /* object */
					 && json_equal(left, scan->first->first))
						break;
				}
			} else {
				for (scan = json_first(right); scan; scan = json_next(scan)) {
					if (json_equal(left, scan))
						break;
				}
			}
			result = json_boolean(scan != NULL);

			/* Just in case right is a deferred array, and we ended
			 * the scan prematurely...
			 */
			json_break(scan);
		}
		break;

	  case JSONOP_FROM:
		/* This is used to fetch the default table for a SELECT
		 * statement that has no explicit FROM clause.  It is handled
		 * by jcsimple(), usually as an argument for the @ operator.
		 * When SELECT is used without columns or WHERE, then we end
		 * up here instead.
		 *
		 * When used with @, we can avoid creating a copy of the
		 * table... BUT NOT HERE!  Since json_calc is returning the
		 * table, it must be something that the calling function can
		 * free.
		 */
		result = jcsimple(calc, context);
		if (result)
			result = json_copy(result);
		else
			result = json_error_null(NULL, "noDefTable:There is no default table for SELECT");
		break;

	  case JSONOP_VALUES:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		result = jcvalues(left, right);
		break;

	  case JSONOP_REGEX:
		/* Using a regular expression where it isn't expected is an error */
		break;

	  case JSONOP_ASSIGN:
		USE_RIGHT_OPERAND(calc);

		/* If error, then just return the error.  Don't assign */
		if (right->type == JSON_NULL && *right->text) {
			/* Errors are never stored in variables, so we know
			 * it's freshly allocated.  Return it as the result
			 * without freeing it.
			 */
			assert(freeright);
			freeright = NULL;
			result = right;
			break;
		}

		/* We always want a copy */
		if (!freeright)
			freeright = right = json_copy(right);

		result = json_context_assign(calc->LEFT, right, context);
		if (result == NULL) {
			/* success, so the right value is still used */
			freeright = NULL;
		}
		break;

	  case JSONOP_MAYBEASSIGN:
		USE_RIGHT_OPERAND(calc);

		/* If the right operand is null, do nothing.  Otherwise... */
		if (!json_is_null(right)) {
			/* We always want a copy */
			if (!freeright)
				freeright = right = json_copy(right);

			result = json_context_assign(calc->LEFT, right, context);
			if (result == NULL) {
				/* success, so the right value is still used */
				freeright = NULL;
			}
		}
		break;

	  case JSONOP_APPEND:
		USE_RIGHT_OPERAND(calc);

		/* We always want a copy */
		if (!freeright)
			freeright = right = json_copy(right);

		result = json_context_append(calc->LEFT, right, context);
		if (result == NULL) {
			/* success, so the right value is still used */
			freeright = NULL;
		}
		break;

	  case JSONOP_STRING:
	  case JSONOP_NUMBER:
	  case JSONOP_BOOLEAN:
	  case JSONOP_NULL:
	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_COMMA:
	  case JSONOP_INVALID:
	  case JSONOP_SELECT:
	  case JSONOP_AS:
	  case JSONOP_DISTINCT:
	  case JSONOP_WHERE:
	  case JSONOP_GROUPBY:
	  case JSONOP_HAVING:
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
	  case JSONOP_LIMIT:
	  case JSONOP_MAYBEMEMBER:
		/* These are only used during parsing, not evaluation */
		abort();
	}

	/* If no result, then use null */
	if (!result)
		result = json_null();

	/* Free operands, if appropriate */
	json_free(freeleft);
	json_free(freeright);

	/* Return the result */
	return result;
}
