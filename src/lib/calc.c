#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <regex.h>
#include <jx.h>

/* Use the real version of jx_calc here, not the debugging macro */
#ifdef jx_calc
# undef jx_calc
#endif

/* BIG NOTE ABOUT MEMORY MANAGEMENT
 *
 * My goals for memory management in jx_calc() are that it should be simple
 * and efficient.  "Efficient" here means that it usually won't allocate new
 * jx_t's if it doesn't have to.
 *
 * To keep it simple though, jx_calc() always returns a freshly allocated
 * jx_t tree.  If you call jx_calc(), then you must eventually call
 * jx_free() on the returned value.
 *
 * jx_calc() is recursive, so it will sometimes allocate and free temporary
 * jx_t's.  Some instances where it DOESN'T need to free a jx_t that it
 * uses are:
 *
 *   Literals.  These are in the jxcalc_t expression as JXOP_LITERAL
 *		nodes.  They contain a jx_t tree that's allocated by
 *		jx_calc_parse() and freed by jx_calc_free().
 *
 *   Names.	The values associated with names come from the context,
 *		usually retrieved via jx_context_by_key().  They are
 *		allocated by the code that sets up the context, and freed
 *		when the context is freed.  Expressions sometimes create
 *		local contexts and free them, but that's a separate thing.
 *
 * That's all! So when jx_calc() needs to access a left or right operand,
 * if it can fetch a literal or name then it doesn't need to free it;
 * otherwise (when it must recursively call jx_calc()) it must free
 * the temporary values.
 */

/* These make accessing the left and right operands easier/clearer. */
#define LEFT  u.param.left
#define RIGHT u.param.right

/* These two macros fetch the left and right operands.  They always set the
 * "left" and "right" variables.  If the the value is freshly allocated
 * (meaning jx_calc() is responsible for freeing it) then they also set
 * the "freeleft" and "freeright" variables.
 */
#define USE_LEFT_OPERAND(calc)	if ((left = jcsimple(calc->LEFT, context)) == NULL)\
		left = freeleft = jx_calc(calc->LEFT, context, agdata)
#define USE_RIGHT_OPERAND(calc)	if ((right = jcsimple(calc->RIGHT, context)) == NULL)\
		right = freeright = jx_calc(calc->RIGHT, context, agdata)

/* If calc is a literal or name, then we can retrieve the value without
 * allocating anything.  Do that, and return it.  Otherwise return NULL.
 */
static jx_t *jcsimple(jxcalc_t *calc, jxcontext_t *context)
{
	jx_t *tmp;

	/* If literal then return its value */
	if (calc->op == JXOP_LITERAL)
		return calc->u.literal;

	/* If simple name then look it up */
	if (calc->op == JXOP_NAME)
		return jx_context_by_key(context, calc->u.text, NULL);

	/* We can do name.name too */
	if (calc->op == JXOP_DOT
	 && calc->RIGHT->op == JXOP_NAME
	 && (tmp = jcsimple(calc->LEFT, context)) != NULL
	 && tmp->type == JX_OBJECT)
		return jx_by_key(tmp, calc->RIGHT->u.text);

	/* We can choose a default table when SELECT is used without FROM */
	if (calc->op == JXOP_FROM)
		return jx_context_default_table(context, NULL);

	/* No joy */
	return NULL;
}


/* Implement @= natural join, @< left join, and @> right join */
jx_t *jcnjoin(jx_t *jl, jx_t *jr, int left, int right)
{
	jx_t  *scan, *result, *merge, *lmem, *rmem;
	int	leftmatch;
	char	*rightmatch, *r;

	/* We normally loop over the left argument in the outer loop, and the
	 * right argument in the inner loop.  If right is deferred and left
	 * isn't, then it's more efficient to use the right in the outer loop
	 * so switch the.
	 */
	if (!jx_is_deferred_array(jl) && jx_is_deferred_array(jr)) {
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
	rightmatch = right ? (char *)calloc(jx_length(jr), sizeof(char)) : NULL;

	/* Start with an empty result array */
	result = jx_array();

	/* For each row from the left table... */
	for (jl = jx_first(jl); jl; jl = jx_next(jl)) {
		/* If interrupted, then discard any result so far and return
		 * an error null.
		 */
		if (jx_interrupt) {
			jx_free(result);
			jx_break(jl);
			return jx_error_null(NULL, "intr:Interrupted");
		}

		/* Skip if not an object */
		if (jl->type != JX_OBJECT)
			continue;

		/* For each row from the right table... */
		leftmatch = 0;
		for (scan = jx_first(jr), r = rightmatch; scan; scan = jx_next(scan), r++) {
			/* Skip if not an object */
			if (scan->type != JX_OBJECT)
				continue;

			/* If any members clash, skip this pairing */
			for (lmem = jl->first; lmem; lmem = lmem->next) { /* object */
				rmem = jx_by_key(scan, lmem->text);
				if (rmem && !jx_equal(lmem->first, rmem))
					break;
			}
			if (lmem)
				continue;

			/* Merge the objects */
			merge = jx_copy(jl);
			for (rmem = scan->first; rmem; rmem = rmem->next) { /* object */
				lmem = jx_by_key(merge, rmem->text);
				if (!lmem)
					jx_append(merge, jx_copy(rmem));
			}

			/* Add the merged object to the result */
			jx_append(result, merge);

			/* Remember that there was a match */
			if (right)
				*r = 1;
			leftmatch = 1;
		}

		/* If doing a left join and left didn't match anything, add it
		 * by itself.
		 */
		if (left && !leftmatch)
			jx_append(result, jx_copy(jl));
	}

	/* If doing a right join, add any right elements that didn't match
	 * anything from the left.
	 */
	if (right) {
		for (scan = jx_first(jr), r = rightmatch; scan; scan = jx_next(scan), r++) {
			if (!*r)
				jx_append(result, jx_copy(scan));
		}
	}

	/* Return the result */
	return result;
}

/* Combine keys and values */
static jx_t *jcvalues(jx_t *keys, jx_t *values)
{
	jx_t	*key, *value, *vrow, *rowobj, *result;

	/* First argument must be an array of strings to use as keys */
	if (keys->type != JX_ARRAY)
		key = keys; /* not NULL marking the end of happy scan */
	else {
		for (key = jx_first(keys); key; key = jx_next(key)) {
			if (key->type != JX_STRING) {
				jx_break(key);
				break;
			}
		}
	}
	if (key || !keys->first)
		return jx_error_null(NULL, "valuesL:Left of VALUES must be an array of strings");

	/* Right argument may be either an array of values, or any array of
	 * arrays of values.  If all elements are arrays then assume the latter.
	 */
	if (values->type != JX_ARRAY)
		return jx_error_null(NULL, "valuesR:Right of VALUES must be array of values, or array of arrays of values");
	for (value = jx_first(values); value; value = jx_next(value)) {
		if (value->type != JX_ARRAY) {
			/* Stop scanning the outer for-loop */
			jx_break(value);

			/* Do the single object version */
			result = jx_object();
			for (key = jx_first(keys), value = jx_first(values); key && value; key = jx_next(key), value = jx_next(value))
				jx_append(result, jx_key(key->text, jx_copy(value)));

			/* If the number keys doesn't match number of values,
			 * then one of the first/next loops ended prematurely.
			 * Clean up!
			 */
			jx_break(key);
			jx_break(value);
			return result;
		}
	}

	/* We'll be doing the table version. */
	result = jx_array();
	for (vrow = jx_first(values); vrow; vrow = jx_next(vrow)) {
		rowobj = jx_object();
		for (key = jx_first(keys), value = jx_first(vrow);
		     key && value;
		     key = jx_next(key), value = jx_next(value))
			jx_append(rowobj, jx_key(key->text, jx_copy(value)));
		jx_break(key);
		jx_break(value);
		jx_append(result, rowobj);
	}
	return result;
}


/* Invoke all aggregates for the current item ("this" in context) */
static void jcag(jxag_t *ag, jxcontext_t *context, void *agdata)
{
	int     i;
	void    *fnag = agdata;
	jx_t  *args;

	/* For each aggregate function... */
	for (i = 0; i < ag->nags; i++) {
		/* Evaluate its parameters */
		args = jx_calc(ag->ag[i]->u.func.args, context, agdata);

		/* Aggregate functions can either accumulate data over rows
		 * of a table *OR* over an array passed as the first argument.
		 * Here we're accumulating, but if the first argument is an
		 * array then the accumulated result will be ignored so we
		 * might as well skip it.
		 */
		if (args->first->type != JX_ARRAY) {
			/* Call the aggregator function */
			ag->ag[i]->u.func.jf->agfn(args, fnag);
		}

		/* Free its parameters */
		jx_free(args);

		/* Find the location of the next function's storage */
		fnag = (void *)((char *)fnag + ag->ag[i]->u.func.jf->agsize);
	}
}

/* If calc uses aggregates, then allocate storage space for them and return
 * a pointer to that... or if existingag is non-NULL then reset it and return
 * it.  Otherwise return NULL to indicate that no aggregates are used.
 * Later, you can free the memory by calling jx_calc_ag(NULL, ag) even if
 * ag is NULL.
 */
void *jx_calc_ag(jxcalc_t *calc, void *existingag)
{
	/* If passed an existingag, then we're either about to free it or
	 * reset it.  Either way, maybe some functions want us to free up
	 * some of allocated data for them.
	 */
	if (existingag) {
		/* There's a list of ag function calls before the data.  Get it.
		 *
		 */
		jxcalc_t *ag = ((jxcalc_t **)existingag)[-1];
		int	i;
		char	*data;
		void	*toFree;

		/* For each function call... */
		for (i = 0, data = (char *)existingag; i < ag->u.ag->nags; data += ag->u.ag->ag[i++]->u.func.jf->agsize) {
			jxfunc_t *jf = ag->u.ag->ag[i]->u.func.jf;
			/* Supposed to free anything? */
			if (jf->jfoptions & JXFUNC_JXFREE) {
				jx_t *doomed = *(jx_t **)data;
				jx_free(doomed);
				toFree = *(void **)(data + sizeof(jx_t *));

			} else
				toFree = *(void **)data;
			if ((jf->jfoptions & JXFUNC_FREE) && toFree) {
				free(toFree);
			}
		}
	}

	/* Passing NULL for calc just means we should free existingag, if any */
	if (!calc) {
		if (existingag)
			free((char*)existingag - sizeof(jxcalc_t**));
		return NULL;
	}

	/* If no aggregates are used, then return NULL */
	if (calc->op != JXOP_AG)
		return NULL;

	/* If no existingag, then allocate it now.  Also add space for a pointer */
	if (!existingag) {
		existingag = malloc(sizeof(jxcalc_t **) + calc->u.ag->agsize) + sizeof(jxcalc_t ***);
	}

	/* Reset it */
	memset(existingag, 0, calc->u.ag->agsize);
	((jxcalc_t **)existingag)[-1] = calc;
	return existingag;
}
 
/* If a jxcalc_t uses aggregate functions, then incorporate this row's
 * data into the aggregates.  If it doesn't use aggregates then do nothing.
 */
void jx_calc_ag_row(jxcalc_t *calc, jxcontext_t *context, void *agdata, jx_t *row)
{
	jxcontext_t *local;

	/* If no aggregates, then do nothing. */
	if (calc->op != JXOP_AG)
		return;

	/* We must have agdata if we're using aggregates. */
	assert(agdata != NULL);

	/* Create a context with this row's data in it, and evaluate all
	 * aggretators with that.
	 */
	local = jx_context(context, row, JX_CONTEXT_THIS | JX_CONTEXT_NOFREE);
	jcag(calc->u.ag, local, agdata);
	jx_context_free(local);
}

/* This implements the @ and @@ operators.  "arr" is normally an array of items
 * to loop over, but it can also be a single item to treat as a singleton array.
 * "expr" is an expression to apply to each member of the array (which may
 * include aggregate functions), and op is either JXOP_EACH or JXOP_GROUP.
 */
jx_t *jceach(jx_t *arr, jxcalc_t *calc, jxcontext_t *context, jxop_t op)
{
	jx_t	*scan, *gscan;
	jx_t	*result, *tmp;
	jxcontext_t *local;
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
	ag = jx_calc_ag(calc, NULL);
	if (ag) {
		/* STEP 1: Count groups, and watch for any ungrouped elements */
		for (ngroups = nongroup = 0, scan = jx_first(arr); scan; scan = jx_next(scan)) {
			if (scan->type == JX_ARRAY)
				ngroups++;
			else
				nongroup++;
		}

		/* STEP 2: Allocate an array to hold groups' aggregate data */
		if (ngroups > 0) {
			groupag = calloc(ngroups, sizeof(void *));
			for (g = 0; g < ngroups; g++)
				groupag[g] = jx_calc_ag(calc, NULL);
		}

		/* STEP 3: Loop over the array to generate aggregate data. */
		for (g = 0, scan = jx_first(arr); scan; scan = jx_next(scan)) {
			if (jx_interrupt) {
				jx_break(scan);
				return jx_error_null(NULL, "intr:Interrupted");
			}

			/* Is this element a nested array? */
			if (scan->type == JX_ARRAY) {
				/* Loop over the array elements */
				for (gscan = jx_first(scan); gscan; gscan = jx_next(gscan)) {
					if (jx_interrupt) {
						jx_break(gscan);
						jx_break(scan);
						return jx_error_null(NULL, "intr:Interrupted");
					}

					/* Invoke the aggregators on "this" */
					local = jx_context(context, gscan, JX_CONTEXT_THIS | JX_CONTEXT_NOFREE);
					jcag(calc->u.ag, local, groupag[g]);
					if (nongroup)
						jcag(calc->u.ag, local, ag);
					jx_context_free(local);
				}

				/* Prepare for next group */
				g++;
			} else {
				/* Invoke the aggregators on "this" */
				local = jx_context(context, scan, JX_CONTEXT_THIS | JX_CONTEXT_NOFREE);
				jcag(calc->u.ag, local, ag);
				jx_context_free(local);
			}
		}
	}

	/* Loop over the array.  For each element, make it "this" and
	 * evaluate the right operand.  Collect the results in a new
	 * array.
	 */
	result = jx_array();
	for (g = 0, scan = jx_first(arr); scan; scan = jx_next(scan)) {
		/* Is it a group (nested array) ? */
		if (scan->type == JX_ARRAY) {
			/* Process the group using the group's own aggregate
			 * data.  For EACH process all of them, for GROUP only
			 * process the first.
			 */
			for (gscan = jx_first(scan); gscan; gscan = (op == JXOP_EACH ? jx_next(gscan) : NULL)) {
				/* If interrupted then discard results so far
				 * and return an error null.
				 */
				if (jx_interrupt) {
					jx_free(result);
					jx_break(gscan);
					jx_break(scan);
					return jx_error_null(NULL, "intr:Interrupted");
				}

				/* Evaluate with element as "this" */
				local = jx_context(context, gscan, JX_CONTEXT_THIS | JX_CONTEXT_NOFREE);
				tmp = jx_calc(calc, local, ag ? groupag[g] : NULL);
				jx_context_free(local);

				/* If null/false, skip it, if true add element*/
				if (tmp->type == JX_NULL || tmp->type == JX_BOOLEAN) {
					/* Skip for null or false, add for true */
					if (jx_is_true(tmp))
						jx_append(result, jx_copy(gscan));
					jx_free(tmp);
				} else {
					/* Not a symbol, append whatever it is */
					jx_append(result, tmp);
				}
			}
			jx_break(gscan); /* since JXOP_GROUP stops early */

			/* Prepare for the next group */
			g++;
		} else {
			/* If interrupted then discard results so far
			 * and return an error null.
			 */
			if (jx_interrupt) {
				jx_free(result);
				return jx_error_null(NULL, "intr:Interrupted");
			}

			local = jx_context(context, scan, JX_CONTEXT_THIS | JX_CONTEXT_NOFREE);
			tmp = jx_calc(calc, local, ag);
			jx_context_free(local);
			if (tmp->type == JX_NULL || tmp->type == JX_BOOLEAN) {
				/* Skip for null or false, add for true */
				if (jx_is_true(tmp))
					jx_append(result, jx_copy(scan));
				jx_free(tmp);
			} else {
				/* Not a symbol, append whatever it is */
				jx_append(result, tmp);
			}
		}
	}

	/* Clean up */
	jx_calc_ag(NULL, ag);
	if (ngroups > 0) {
		for (g = 0; g < ngroups; g++)
			jx_calc_ag(NULL, groupag[g]);
		free(groupag);
	}

	/* Done! */
	return result;
}


/* Evaluate an expression and return the result.
 *   calc       The expression to evaluate.  This should be obtained from a 
 *              previous call to jx_calc_parse().
 *   context    A list of objects providing context for the expression.
 *              The first element is "this".  Any element that's an object
 *              can be scanned to obtain variable names.  May be NULL.
 *   agdata     Storage space for aggregate functions, allocated by
 *              jx_calc_ag(calc, NULL), freed by jx_calc_ag(NULL, ag);
 *
 * NOTE: For runtime errors, this mostly returns a "null" jx_t node
 * containing an error message in ->text, and the error code in ->first.
 */
jx_t *jx_calc(jxcalc_t *calc, jxcontext_t *context, void *agdata)
{
	jx_t *left, *right, *freeleft, *freeright;
	jx_t *result;
	jxcalc_t *tmp;
	jx_t  *scan, *found;
	double  nl, nr;
	int     il,ir;
	char    *str;
	void    *localag;
	jxfuncextra_t recon;

	/* If interrupted then simply return an error null */
	if (jx_interrupt)
		return jx_error_null(NULL, "intr:Interrupted");

	/* Start with freeleft and freeleft set to NULL.  The USE_LEFT_OPERAND
	 * and USE_RIGHT_OPERAND macros will set them if appropriate.
	 */
	freeleft = freeright = result = NULL;

	/* Process the expression */
	switch (calc->op)
	{
	  case JXOP_LITERAL:
		result = jx_copy(calc->u.literal);
		break;

	  case JXOP_NAME:
		result = jx_copy(jx_context_by_key(context, calc->u.text, NULL));
		break;

	  case JXOP_ENVIRON:
		/* Either $name or $name[subscr].  calc->LEFT is always a
		 * JXOP_NAME, and calc->RIGHT is NULL or subscript expression.
		 */
		assert(calc->LEFT->op == JXOP_NAME);
		if (calc->RIGHT) {
			char	*name, *sub;
			USE_RIGHT_OPERAND(calc);
			if (right->type == JX_STRING
			 || right->type == JX_BOOLEAN
			 || (right->type == JX_NUMBER && right->text[0])) {
				name = (char *)malloc(strlen(calc->LEFT->u.text) + strlen(right->text) + 1);
				strcpy(name, calc->LEFT->u.text);
				strcat(name, right->text);
				str = getenv(name);
				free(name);
			} else {
				sub = jx_serialize(right, NULL);
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

		/* If we found a value, then convert it to a jx_t;
		 * otherwise return a null.
		 */
		if (str)
			result = jx_string(str, -1);
		else
			result = jx_null();
		str = NULL;
		break;

	  case JXOP_ARRAY:
		/* Append the value of each element into an array. */
		result = jx_array();
		if (calc->LEFT)
			jx_append(result, jx_calc(calc->LEFT, context, agdata));
		for (tmp = calc->RIGHT; tmp; tmp = tmp->RIGHT)
			jx_append(result, jx_calc(tmp->LEFT, context, agdata));
		break;

	  case JXOP_OBJECT:
		/* Append name:value pairs into an object */
		result = jx_object();
		if (calc->LEFT) {
			/* calc->LEFT is the first name:value.
			 * tmp is the name, with op=JXOP_NAME.
			 * right is the value, to evaluate via jx_calc()
			 */
			tmp = calc->LEFT->LEFT;
			found = jx_calc(calc->LEFT->RIGHT, context, agdata);
			if (calc->LEFT->op != JXOP_MAYBEMEMBER || !jx_is_null(found))
				jx_append(result, jx_key(tmp->u.text, found));
			else
				jx_free(found);
		}
		for (calc = calc->RIGHT; calc; calc = calc->RIGHT) {
			/* calc->LEFT is the next name:value.
			 * tmp is the name, with op=JXOP_NAME.
			 * right is the value, to evaluate via jx_calc()
			 */
			tmp = calc->LEFT->LEFT;
			found = jx_calc(calc->LEFT->RIGHT, context, agdata);
			if (calc->LEFT->op != JXOP_MAYBEMEMBER || !jx_is_null(found))
				jx_append(result, jx_key(tmp->u.text, found));
			else
				jx_free(found);
		}
		return result;

	  case JXOP_SUBSCRIPT:
		USE_LEFT_OPERAND(calc);
		if (calc->RIGHT->op == JXOP_COLON) {
			char *key;

			/* Subscript by name:value, scans an array of objects
			 * for a given member and value.
			 */
			if (left->type != JX_ARRAY)
				break;

			/* Evaluate the value of name:value.  Also fetch name */
			USE_RIGHT_OPERAND(calc->RIGHT);
			key = calc->RIGHT->LEFT->u.text;

			/* Scan array for element with that member name:value */
			str = NULL;
			for (scan = jx_first(left); scan; scan = jx_next(scan)) {
				if (scan->type != JX_OBJECT)
					continue;
				found = jx_by_key(scan, key);
				if (found && found->type == JX_STRING && right->type == JX_STRING) {
					/* String comparison is case-insensitive */
					if (!jx_mbs_casecmp(found->text, right->text)) {
						result = jx_copy(scan);
						jx_break(scan);
						break;
					}
				} else if (found && found->type == JX_STRING && right->type != JX_STRING) {
					/* This handles the special case where
					 * the value we're searching for is a
					 * number, but the data we're comparing
					 * it to is a string.  We convert the
					 * search value to a string ONCE, and
					 * do a string comparison.
					 */
					if (!str)
						str = jx_serialize(right, NULL);
					if (!jx_mbs_casecmp(found->text, str)) {
						result = jx_copy(scan);
						jx_break(scan);
						break;
					}

				} else if (found && jx_equal(found, right)) {
					result = jx_copy(scan);
					jx_break(scan);
					break;
				}
			}
			if (str)
				free(str);
			break;
		} else {
			/* Evaluate the subscript.  Strings only work for
			 * objects, numbers only work for arrays or strings.
			 */
			USE_RIGHT_OPERAND(calc);
			if (left->type == JX_OBJECT && right->type == JX_STRING)
				result = jx_by_key(left, right->text);
			else if (left->type == JX_OBJECT) {
				/* convert to a string, use it as the key */
				str = jx_serialize(right, NULL);
				result = jx_by_key(left, str);
				free(str);
			} else if (left->type == JX_ARRAY && right->type == JX_NUMBER)
				result = jx_by_index(left, jx_int(right));
			else if (left->type == JX_STRING && right->type == JX_NUMBER) {
				size_t len = strlen(left->text);
				size_t end = 1; /* single character */
				ir = jx_int(right);
				if (ir < 0)
					ir += len;
				if (ir >= 0 && ir < len) {
					const char *str = jx_mbs_substr(left->text, ir, &end);
					result = jx_string(str, end);
					break;
				}
			}
		}

		/* Use a copy of the result.  Also, call jx_break() on it,
		 * just in case it came from a deferred array.
		 */
		found = jx_copy(result);
		jx_break(result);
		result = found;
		break;

	  case JXOP_FNCALL:
		/* Collect parameter values into an array */
		freeleft = left = jx_calc(calc->u.func.args, context, agdata);

		/* Aggregate functions are special, if the first parameter is
		 * an array.  (The parser can't always tell whether the first
		 * parameter is going to be an array, so it'll create a
		 * JXOP_AG node above this which may result it data being
		 * accumulated that way.  But if passed an array, it'll ignore
		 * that aggregated data and create new aggregated data from
		 * the array.)
		 */
		if (left->first->type == JX_ARRAY && calc->u.func.jf->agfn) {
			jxfunc_t *jf = calc->u.func.jf;
			void **toFree;

			/* Allocate storage for the function */
			localag = malloc(jf->agsize);
			memset(localag, 0, jf->agsize);

			/* For each element of the array, create a new parameter
			 * list and call the aggregator.  Note that we don't
			 * need to create a new context, because all parameters
			 * have already been calculated.
			 */
			found = jx_array();
			for (scan = jx_first(left->first); scan; scan = jx_next(scan)) { /* undeferred */
				/* Create a new argument list.  The first is an
				 * element from the array, and any other args
				 * are used unchanged. To accomplish this, we
				 * will temporarily mangle scan's "next".
				 */
				jx_t *scannext = scan->next; /* undeferred */
				scan->next = left->first->next; /* undeferred */
				found->first = scan;

				/* Invoke the aggregator */
				(*jf->agfn)(found, localag);

				/* Restore scan's "next" pointer */
				scan->next = scannext; /* undeferred */
			}
			found->first = NULL;
			jx_free(found);

			/* Invoke the function */
			result = (*jf->fn)(left, localag);

			/* Clean up */
			if (jf->jfoptions & JXFUNC_JXFREE) {
				jx_free(*(jx_t **)localag);
				toFree = (void **)((jx_t **)localag + 1);
			} else
				toFree = (void **)localag;
			if (jf->jfoptions & JXFUNC_FREE && *toFree != NULL)
				free(*toFree);
			free(localag);
		} else {
			/* Non-aggregate built-in functions may take a regular
			 * expression.  Since that isn't a JSON data type,
			 * the args list will just contain "null" there; we
			 * need to scan the argument array generator for a
			 * JXOP_REGEX... but only for non-aggregate built-ins.
			 */
			localag = (void *)((char *)agdata + calc->u.func.agoffset);
			if (!calc->u.func.jf->agfn && !calc->u.func.jf->user) {
				recon.context = context;
				recon.regex = NULL;
				if (!calc->u.func.jf->user) {
					tmp = calc->u.func.args;
					if (tmp->LEFT && tmp->LEFT->op == JXOP_REGEX)
						tmp = tmp->LEFT;
					else for (tmp = tmp->RIGHT; tmp; tmp = tmp->RIGHT)
						if (tmp->LEFT->op == JXOP_REGEX) {
							tmp = tmp->LEFT;
							break;
					}
					recon.regex = (void *)tmp;
				}
				localag = (void *)&recon;
			}

			/* Invoke the function. For built-ins, call the
			 * function directly ("jf->fn").  For user-defined
			 * functions, call jx_cmd_fncall() to do it.
			 */
			if (calc->u.func.jf->user)
				result = jx_cmd_fncall(left, calc->u.func.jf, context);
			else if (calc->u.func.jf->fn)
				result = (*calc->u.func.jf->fn)(left, localag);
			else
				result = NULL; /* probably an empty user func */
		}
		break;

	  case JXOP_AG:
		/* We always expect agdata when we're using aggregates, but
		 * if we aren't given agdata then use blank agdata.  We won't
		 * get useful results that way, but at least we won't dump core.
		 */
		if (!agdata) {
			/* Evaluate using blank agdata */
			localag = jx_calc_ag(calc, NULL);
			result = jx_calc(calc->u.ag->expr, context, localag);
			localag = jx_calc_ag(NULL, localag);
		} else {
			/* Evaluate expr, but use *this* agdata to do it */
			result = jx_calc(calc->u.ag->expr, context, agdata);
		}
		break;

	  case JXOP_FIND:
		/* Evaluate the left operand.  Then pass that result and the
		 * right operand to jx_find_calc() to build the result table.
		 */
		USE_LEFT_OPERAND(calc);
		if (jx_is_null(left))
			result = left;
		else
			result = jx_find_calc(left, calc->RIGHT, context);
		break;

	  case JXOP_EACH:
	  case JXOP_GROUP:
		/* Evaluate the left operand.  If null then return an empty
		 * array.  If it is an array then set scan to its first
		 * element; if not an array then set scan to it directly,
		 * so it'll effectively be treated like a single-element array.
		 */
		USE_LEFT_OPERAND(calc);
		if (jx_is_null(left)) {
			result = jx_array();
			break;
		}

		/* Do the thing */
		result = jceach(left, calc->RIGHT, context, calc->op);
		break;

	  case JXOP_NJOIN:
	  case JXOP_LJOIN:
	  case JXOP_RJOIN:
		/* Natural join of left and right arrays.  The pairing-up
		 * logic is implemented in jcnjoin(), but we still have a bit
		 * of operand evaluation and cleanup to worry about here.
		 */
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		result = jcnjoin(left, right, calc->op == JXOP_LJOIN, calc->op == JXOP_RJOIN);
		/* NOTE: jcnjoin() always copies any data it uses.  Nothing
		 * in it could still be used by jl or jr.
		 */
		break;

	  case JXOP_DOT:
		/* NOTE: Function calls of the form data.func(args...) are
		 * transformed to func(data, args...) during parsing, so we
		 * only see the . operator while looking for a member of an
		 * object.
		 */
		assert(calc->RIGHT->op == JXOP_NAME);
		USE_LEFT_OPERAND(calc);
		if (left->type == JX_OBJECT && (result = jx_by_key(left, calc->RIGHT->u.text)) != NULL)
			result = jx_copy(result);
		else if (!strcasecmp(calc->RIGHT->u.text, "length")) {
			/* The "length" attribute is computed, for strings and
			 * arrays.  To simplify processing of data that was
			 * converted from XML, we also return 0 for null.length
			 * and 1 for anything_else.length -- XML doesn't do
			 * arrays very well.
			 */
			if (left->type == JX_ARRAY)
				result = jx_from_int(jx_length(left));
			else if (left->type == JX_STRING)
				result = jx_from_int(jx_mbs_len(left->text));
			else if (jx_is_null(left))
				result = jx_from_int(0);
			else
				result = jx_from_int(1);
		}
		break;

	  case JXOP_DOTDOT:
		USE_LEFT_OPERAND(calc);
		if (left->type != JX_OBJECT && calc->RIGHT->op == JXOP_NAME)
			result = jx_copy(jx_by_deep_key(left, calc->RIGHT->u.text));
		break;

	  case JXOP_ELLIPSIS:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			il = jx_int(left);
			ir = jx_int(right);
#if 0
			result = jx_array();
			for (; il <= ir; il++) {
				jx_append(result, jx_from_int(il));
			}
#else
			result = jx_defer_ellipsis(il, ir);
#endif
		}
		break;

	  case JXOP_COALESCE:
		/* If left arg is non-null, return it */
		USE_LEFT_OPERAND(calc);
		if (!jx_is_null(left)) {
			if (freeleft) {
				result = left;
				freeleft = NULL;
			} else
				result = jx_copy(left);
			break;
		}

		/* Else return right arg */
		USE_RIGHT_OPERAND(calc);
		if (freeright) {
			result = right;
			freeright = NULL;
		} else
			result = jx_copy(right);
		break;

	  case JXOP_QUESTION:
		USE_LEFT_OPERAND(calc);
		/* Can be test?then or test?them:else */
		if (calc->RIGHT->op == JXOP_COLON) {
			if (jx_is_true(left))
				result = jx_calc(calc->RIGHT->LEFT, context, agdata);
			else
				result = jx_calc(calc->RIGHT->RIGHT, context, agdata);
		} else {
			if (jx_is_true(left)) {
				USE_RIGHT_OPERAND(calc);
				if (freeright) {
					result = freeright;
					freeright = NULL;
				} else {
					result = jx_copy(right);
				}
			}
		}
		break;

	  case JXOP_COLON:
		/* Shouldn't happen. */
		abort();

	  case JXOP_ISNULL:
		USE_RIGHT_OPERAND(calc);
		result = jx_boolean(jx_is_null(right));
		break;

	  case JXOP_ISNOTNULL:
		USE_RIGHT_OPERAND(calc);
		result = jx_boolean(!jx_is_null(right));
		break;

	  case JXOP_NEGATE:
		USE_RIGHT_OPERAND(calc);
		if (right->type == JX_NUMBER || right->type == JX_STRING)
			result = jx_from_double(-jx_double(right));
		break;

	  case JXOP_ADD:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (jx_is_date(left) && jx_is_period(right)) {
			/* ISO datetime+period.  Add the period to the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			jx_datetime_add(buf, left->text, right->text);
			buf[10] = '\0';
			result = jx_string(buf, -1);
		} else if ((jx_is_date(left) || jx_is_datetime(left)) && jx_is_period(right)) {
			/* ISO datetime+period.  Add the period to the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			jx_datetime_add(buf, left->text, right->text);
			result = jx_string(buf, -1);
		} else if (left->type == JX_STRING || right->type == JX_STRING) {
			/* String version.  If one of the operands is a
			 * non-string, then convert it to a string.
			 * One minor optimization is that if a number is in
			 * text form, or a boolean, then we can treat it as
			 * a string already.
			 */
			if ((left->type == JX_STRING || left->type == JX_BOOLEAN || (left->type == JX_NUMBER && *left->text))
			 && (right->type == JX_STRING || right->type == JX_BOOLEAN || (right->type == JX_NUMBER && *right->text))) {
				/* Both are strings, or at least stringy */
				result = jx_string(left->text, strlen(left->text) + strlen(right->text));
				strcat(result->text, right->text);
			} else if (left->type == JX_NULL) {
				if (freeright) {
					result = right;
					freeright = NULL;
				} else
					result = jx_copy(right);
			} else if (right->type == JX_NULL) {
				if (freeleft) {
					result = left;
					freeleft = NULL;
				} else
					result = jx_copy(left);
			} else if (left->type != JX_STRING) {
				/* Left operand needs to be converted */
				str = jx_serialize(left, NULL);
				result = jx_string(str, strlen(str) + strlen(right->text));
				strcat(result->text, right->text);
				free(str);
			} else { /* Right is not stringy */
				/* Right operand needs to be converted */
				str = jx_serialize(right, NULL);
				result = jx_string(left->text, strlen(left->text) + strlen(str));
				strcat(result->text, str);
				free(str);
			}
		}
		else if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			/* Number version */
			result = jx_from_double(jx_double(left) + jx_double(right));
		}
		break;

	  case JXOP_SUBTRACT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (jx_is_date(left) && jx_is_period(right)) {
			/* ISO date-period.  Subtract the period from the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			jx_datetime_subtract(buf, left->text, right->text);
			buf[10] = '\0';
			result = jx_string(buf, -1);
		} else if (jx_is_datetime(left) && jx_is_period(right)) {
			/* ISO datetime-period.  Subtract the period from the
			 * datetime, and return the resulting datetime.
			 * Note that both the datetime and period are strings.
			 */
			char buf[50];
			jx_datetime_subtract(buf, left->text, right->text);
			result = jx_string(buf, -1);
		} else if ((jx_is_date(left) || jx_is_datetime(left))
		        && (jx_is_date(right) || jx_is_datetime(right))) {
			/* ISO datetime-datetime.  Find the difference between
			 * two dates, and return it as a period.
			 * Note that the datetimes are strings.
			 */
			char buf[50];
			jx_datetime_diff(buf, left->text, right->text);
			result = jx_string(buf, -1);
		} else if (left->type == JX_STRING || right->type == JX_STRING) {
			/* String version.  If one of the operands is a
			 * non-string, then convert it to a string.
			 * One minor optimization is that if a number is in
			 * text form, or a boolean, then we can treat it as
			 * a string already.
			 */
			char	*leftstr, *rightstr;
			size_t	leftlen;
			str = NULL;
			if ((left->type == JX_STRING || left->type == JX_BOOLEAN || (left->type == JX_NUMBER && *left->text))
			 && (right->type == JX_STRING || right->type == JX_BOOLEAN || (right->type == JX_NUMBER && *right->text))) {
				/* Both are strings, or at least stringy */
				leftstr = left->text;
				rightstr = right->text;
			} else if (left->type == JX_NULL) {
				if (freeright) {
					result = right;
					freeright = NULL;
				} else
					result = jx_copy(right);
				break;
			} else if (right->type == JX_NULL) {
				if (freeleft) {
					result = left;
					freeleft = NULL;
				} else
					result = jx_copy(left);
				break;
			} else if (left->type != JX_STRING) {
				/* Left operand needs to be converted */
				str = jx_serialize(left, NULL);
				leftstr = str;
				rightstr = right->text;
			} else { /* Right is not stringy */
				/* Right operand needs to be converted */
				str = jx_serialize(right, NULL);
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
			result = jx_string(leftstr, leftlen + 1 + strlen(rightstr));
			if (leftlen > 0 && *rightstr)
				result->text[leftlen++] = ' ';
			strcpy(result->text + leftlen, rightstr);

			/* If we had to serialize a value, free that now */
			if (str)
				free(str);
		}
		else if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			/* Number version */
			result = jx_from_double(jx_double(left) - jx_double(right));
		}
		break;

	  case JXOP_MULTIPLY:
	  case JXOP_DIVIDE:
	  case JXOP_MODULO:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			/* Convert to binary */
			nl = jx_double(left);
			nr = jx_double(right);

			/* Do the math */
			if (calc->op == JXOP_MULTIPLY)
				result = jx_from_double(nl * nr);
			else if (nr == 0.0)
				result = jx_error_null(NULL, "div0:division by 0");
			else if (calc->op == JXOP_DIVIDE)
				result = jx_from_double(nl / nr);
			else if ((int)nr == 0)
				result = jx_error_null(NULL, "mod0:modulo by 0");
			else /* JXOP_MODULO */
				result = jx_from_double((int)nl % (int)nr);
		}
		break;

	  case JXOP_BITNOT:
		USE_RIGHT_OPERAND(calc);
		if (right->type == JX_NUMBER)
			result = jx_from_int(~jx_int(right));
		break;

	  case JXOP_BITAND:
	  case JXOP_BITOR:
	  case JXOP_BITXOR:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			/* Convert to binary */
			il = jx_int(left);
			ir = jx_int(right);

			/* Do the bitwise math */
			if (calc->op == JXOP_BITAND)
				result = jx_from_int(il & ir);
			else if (calc->op == JXOP_BITOR)
				result = jx_from_int(il | ir);
			else /* JXOP_BITOR */
				result = jx_from_int(il ^ ir);
		} else if (left->type == JX_OBJECT && right->type == JX_OBJECT) {
			if (calc->op == JXOP_BITAND) {
				/* Keep left keys/values only if same key is in right */
				result = jx_object();
				for (scan = left->first; scan; scan = scan->next) { /* object */
					if (jx_by_key(right, scan->text))
						jx_append(result, jx_copy(scan));
				}
			} else if (calc->op == JXOP_BITOR) {
				/* Merge right keys/values into left */
				result = jx_copy(left);
				for (scan = right->first; scan; scan = scan->next) { /* object */
					jx_append(result, jx_copy(scan));
				}
			} else { /* JXOP_BITOR */
				/* Keep left keys/values only if key is NOT in right */
				result = jx_object();
				for (scan = left->first; scan; scan = scan->next) { /* object */
					if (!jx_by_key(right, scan->text))
						jx_append(result, jx_copy(scan));
				}
			}
		}
		break;

	  case JXOP_NOT:
		USE_RIGHT_OPERAND(calc);
		result = jx_boolean(!jx_is_true(right));
		break;

	  case JXOP_AND:
	  case JXOP_OR:
		USE_LEFT_OPERAND(calc);
		il = jx_is_true(left);
		if (calc->op == (il ? JXOP_AND : JXOP_OR)) {
			USE_RIGHT_OPERAND(calc);
			il = jx_is_true(right);
		}
		result = jx_boolean(il);
		break;

	  case JXOP_EQSTRICT:
	  case JXOP_NESTRICT:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Compare them using jx_equal(), which checks data types.
		 * It also does a "deep" comparison, allowing you to compare
		 * the contents of arrays, or of objects.
		 */
		il = jx_equal(left, right);
		if (calc->op == JXOP_NESTRICT)
			il = !il;
		result = jx_boolean(il);
		break;

	  case JXOP_LT:
	  case JXOP_LE:
	  case JXOP_EQ:
	  case JXOP_NE:
	  case JXOP_GE:
	  case JXOP_GT:
	  case JXOP_ICEQ:
	  case JXOP_ICNE:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Arrays and objects can't be compared this way.  They can
		 * be compared for strict equality, but not this.
		 */
		if (left->type == JX_ARRAY || left->type == JX_OBJECT
		 || right->type == JX_ARRAY || right->type == JX_OBJECT) {
			result = jx_error_null(NULL, "cmpObjArr:Can't compare objects/arrays except via === or !==");
			break;
		}

		/* Compare them in an appropriate way */
		if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			nl = jx_double(left);
			nr = jx_double(right);
			if (nl < nr)
				il = -1;
			else if (nl > nr)
				il = 1;
			else
				il = 0;
		} else if ((left->type == JX_BOOLEAN || right->type == JX_BOOLEAN)
		        && (calc->op == JXOP_EQ || calc->op == JXOP_NE)) {
			/* Compare as booleans, but only for equality */
			il = jx_is_true(left) != jx_is_true(right);
		} else if (left->type == JX_NULL || right->type == JX_NULL){
		        /* We allow equality comparisons to null. Anything else
		         * is always false.
		         */
		        if (calc->op == JXOP_EQ || calc->op == JXOP_NE)
				il = (left->type != right->type);
			else {
				result = jx_boolean(0);
				break;
			}
		} else if ((left->type == JX_NUMBER && right->type == JX_STRING)
			|| (left->type == JX_STRING && right->type == JX_NUMBER)) {
			/* When comparing strings and numbers, convert the
			 * string to a number.
			 */
			if (left->type == JX_NUMBER)
				nl = jx_double(left);
			else {
				nl = strtod(left->text, &str);
				if (*str) {
					/* Not a clean conversion, so not equal */
					result = jx_boolean(calc->op == JXOP_NE || calc->op == JXOP_ICNE);
					break;
				}
			}
			if (right->type == JX_NUMBER)
				nr = jx_double(right);
			else {
				nr = strtod(right->text, &str);
				if (*str) {
					/* Not a clean conversion, so not equal */
					result = jx_boolean(calc->op == JXOP_NE || calc->op == JXOP_ICNE);
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
			if (calc->op == JXOP_ICEQ || calc->op == JXOP_ICNE){
				size_t lenl, lenr, spacesl, spacesr;

				/* The tricky thing here is that we want to
				 * ignore trailing spaces.  This sounds like
				 * a job for jx_mbs_ncasecmp(), but that
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
				lenl = jx_mbs_len(left->text);
				lenr = jx_mbs_len(right->text);

				/* Compare trimmed lengths.  If not the same
				 * then the strings don't match.  Otherwise we
				 * need to check the characters.
				 */
				if (lenl - spacesl != lenr - spacesr)
					il = 1;	/* trimmed lengths differ */
				else if (lenl - spacesl == 0)
					il = 0; /* both are empty */
				else
					il = jx_mbs_ncasecmp(left->text, right->text, lenl - spacesl);
			} else
				il = strcmp(left->text, right->text);
		}

		/* Choose a comparison */
		switch (calc->op) {
		  case JXOP_EQ:
		  case JXOP_ICEQ: ir = (il == 0); break;
		  case JXOP_NE:
		  case JXOP_ICNE: ir = (il != 0); break;
		  case JXOP_LT:   ir = (il < 0);  break;
		  case JXOP_LE:   ir = (il <= 0); break;
		  case JXOP_GE:   ir = (il >= 0); break;
		  default: /* GT */ ir = (il > 0);  break;
		}

		/* Set the result */
		result = jx_boolean(ir);
		break;

	  case JXOP_BETWEEN:
		assert(calc->RIGHT->op == JXOP_AND);
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
		if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
			if (jx_double(left) < jx_double(right))
				result = jx_boolean(0);
		} else if (left->type == JX_STRING && right->type == JX_STRING) {
			if (jx_mbs_casecmp(left->text, right->text) < 0)
				result = jx_boolean(0);
		} else if ((left->type == JX_NUMBER && right->type == JX_STRING)
			|| (left->type == JX_STRING && right->type == JX_NUMBER)) {
			/* When comparing strings and numbers, convert the
			 * string to a number.
			 */
			if (left->type == JX_NUMBER)
				nl = jx_double(left);
			else {
				nl = strtod(left->text, &str);
				if (*str) {
					/* Not a clean conversion */
					result = jx_boolean(0);
					break;
				}
			}
			if (right->type == JX_NUMBER)
				nr = jx_double(right);
			else {
				nr = strtod(right->text, &str);
				if (*str) {
					/* Not a clean conversion */
					result = jx_boolean(0);
					break;
				}
			}
			if (nl < nr)
				result = jx_boolean(0);
		} else
			result = jx_error_null(NULL, "between:BETWEEN only works on strings and numbers");

		if (freeright) {
			jx_free(freeright);
			freeright = NULL;
		}

		/* Test upper bound.  If we already know the tested value is
		 * below the lower bound, we can skip this.
		 */
		if (!result) {
			USE_RIGHT_OPERAND(calc->RIGHT);
			if (left->type == JX_NUMBER && right->type == JX_NUMBER) {
				if (jx_double(left) > jx_double(right))
					result = jx_boolean(0);
			} else if (left->type == JX_STRING && right->type == JX_NUMBER) {
				if (jx_mbs_casecmp(left->text, right->text) > 0)
					result = jx_boolean(0);
			} else if ((left->type == JX_NUMBER && right->type == JX_STRING)
				|| (left->type == JX_STRING && right->type == JX_NUMBER)) {
				/* When comparing strings and numbers, convert
				 * the string to a number.
				 */
				if (left->type == JX_NUMBER)
					nl = jx_double(left);
				else {
					nl = strtod(left->text, &str);
					if (*str) {
						/* Not a clean conversion */
						result = jx_boolean(0);
						break;
					}
				}
				if (right->type == JX_NUMBER)
					nr = jx_double(right);
				else
					nr = strtod(right->text, &str);
				if (nl > nr) {
					result = jx_boolean(0);
					if (*str) {
						/* Not a clean conversion */
						result = jx_boolean(0);
						break;
					}
				}
			} else
				result = jx_error_null(NULL, "between:BETWEEN only works on strings and numbers");
		}

		/* If no result, I guess we're okay */
		if (!result)
			result = jx_boolean(1);

		break;

	  case JXOP_LIKE:
	  case JXOP_NOTLIKE:
		USE_LEFT_OPERAND(calc);
		if (calc->RIGHT->op == JXOP_REGEX) {
			regmatch_t matches[10];
			if (left->type == JX_STRING
			 && regexec((regex_t *)calc->RIGHT->u.regex.preg, left->text, 10, matches, 0) == 0)
				result = jx_boolean(1);
			else
				result = jx_boolean(0);
		} else  {
			USE_RIGHT_OPERAND(calc);
			if (left->type != JX_STRING || right->type != JX_STRING) {
				result = jx_boolean(0);
			} else {
				il = jx_mbs_like(left->text, right->text);
				if (calc->op == JXOP_NOTLIKE)
					il = !il;
				result = jx_boolean(il);
			}
		}
		break;

	  case JXOP_IN:
	  case JXOP_NOTIN:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);

		/* Scan the right-hand list, looking for an exact match */
		if (right->type == JX_ARRAY) {

			if (left->type == JX_STRING) {
				/* Is "right" a single-column table? */
				if (jx_is_table(right)) {
					/* If single column, check value */
					for (scan = jx_first(right); scan; scan = jx_next(scan)) {
						if (scan->first->first
						 && scan->first->first->type == JX_STRING
						 && scan->first->next == NULL /* object */
						 && !jx_mbs_casecmp(left->text, scan->first->first->text))
							break;
					}
				} else {
					/* compare strings to strings */
					for (scan = jx_first(right); scan; scan = jx_next(scan)) {
						if (scan->type == JX_STRING
						 && !jx_mbs_casecmp(left->text, scan->text))
							break;
					}
				}

			} else if (left->type == JX_NUMBER && jx_is_table(right)) {
				/* If single column, compare to value */
				for (scan = jx_first(right); scan; scan = jx_next(scan)) {
					if (scan->first->next == NULL /* object */
					 && jx_equal(left, scan->first->first))
						break;
				}
			} else {
				for (scan = jx_first(right); scan; scan = jx_next(scan)) {
					if (jx_equal(left, scan))
						break;
				}
			}
			if (calc->op == JXOP_IN)
				result = jx_boolean(scan != NULL);
			else
				result = jx_boolean(scan == NULL);

			/* Just in case right is a deferred array, and we ended
			 * the scan prematurely...
			 */
			jx_break(scan);
		}
		break;

	  case JXOP_FROM:
		/* This is used to fetch the default table for a SELECT
		 * statement that has no explicit FROM clause.  It is handled
		 * by jcsimple(), usually as an argument for the @ operator.
		 * When SELECT is used without columns or WHERE, then we end
		 * up here instead.
		 *
		 * When used with @, we can avoid creating a copy of the
		 * table... BUT NOT HERE!  Since jx_calc is returning the
		 * table, it must be something that the calling function can
		 * free.
		 */
		result = jcsimple(calc, context);
		if (result)
			result = jx_copy(result);
		else
			result = jx_error_null(NULL, "noDefTable:There is no default table for SELECT");
		break;

	  case JXOP_VALUES:
		USE_LEFT_OPERAND(calc);
		USE_RIGHT_OPERAND(calc);
		result = jcvalues(left, right);
		break;

	  case JXOP_REGEX:
		/* Using a regular expression where it isn't expected is an error */
		break;

	  case JXOP_ASSIGN:
		USE_RIGHT_OPERAND(calc);

		/* If error, then just return the error.  Don't assign */
		if (right->type == JX_NULL && *right->text) {
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
			freeright = right = jx_copy(right);

		result = jx_context_assign(calc->LEFT, right, context);
		if (result == NULL) {
			/* success, so the right value is still used */
			freeright = NULL;
		}
		break;

	  case JXOP_MAYBEASSIGN:
		USE_RIGHT_OPERAND(calc);

		/* If the right operand is null, do nothing.  Otherwise... */
		if (!jx_is_null(right)) {
			/* We always want a copy */
			if (!freeright)
				freeright = right = jx_copy(right);

			result = jx_context_assign(calc->LEFT, right, context);
			if (result == NULL) {
				/* success, so the right value is still used */
				freeright = NULL;
			}
		}
		break;

	  case JXOP_APPEND:
		USE_RIGHT_OPERAND(calc);

		/* We always want a copy */
		if (!freeright)
			freeright = right = jx_copy(right);

		result = jx_context_append(calc->LEFT, right, context);
		if (result == NULL) {
			/* success, so the right value is still used */
			freeright = NULL;
		}
		break;

	  case JXOP_STRING:
	  case JXOP_NUMBER:
	  case JXOP_BOOLEAN:
	  case JXOP_NULL:
	  case JXOP_STARTPAREN:
	  case JXOP_ENDPAREN:
	  case JXOP_STARTARRAY:
	  case JXOP_ENDARRAY:
	  case JXOP_STARTOBJECT:
	  case JXOP_ENDOBJECT:
	  case JXOP_COMMA:
	  case JXOP_INVALID:
	  case JXOP_SELECT:
	  case JXOP_AS:
	  case JXOP_DISTINCT:
	  case JXOP_WHERE:
	  case JXOP_GROUPBY:
	  case JXOP_HAVING:
	  case JXOP_ORDERBY:
	  case JXOP_DESCENDING:
	  case JXOP_LIMIT:
	  case JXOP_MAYBEMEMBER:
		/* These are only used during parsing, not evaluation */
		abort();
	}

	/* If no result, then use null */
	if (!result)
		result = jx_null();

	/* Free operands, if appropriate */
	jx_free(freeleft);
	jx_free(freeright);

	/* Return the result */
	return result;
}
