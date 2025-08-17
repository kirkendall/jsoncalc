#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <jsoncalc.h>


/* Return a new array which merges any embedded arrays into new array propper.
 * For example, [1,[2,3],4] would become [1,2,3,4].  Depth can be 0 to just
 * copy without changing anything, 1 for a single layer, 2 for 2 layers deep,
 * or as a special case, -1 means unlimited depth.
 */
json_t *json_array_flat(json_t *array, int depth)
{
	json_t *result;
	json_t *scan, *lag;
	json_t *copy;

	/* Defend against NULL */
	if (!array)
		return NULL;

	/* If not an array, return NULL.  (We don't dare return it unchanged
	 * because this function is intended to return a NEW array, not a
	 * link to existing data.)
	 */
	if (array->type != JSON_ARRAY)
		return NULL;

	/* Start a new array */
	result = json_array();

	/* For each element of the array... */
	for (lag = NULL, scan = array->first; scan; scan = scan->next) {
		/* If depth is 0 or this element isn't array, copy it */
		if (depth == 0 || scan->type != JSON_ARRAY) {
			lag = json_copy(scan);
			json_append(result, lag);
			continue;
		}

		/* Okay, we have an array and we want to include it.  Copy it
		 * via a recursive call to json_array_flat(), and then munge
		 * the pointers to make it appear after "lag".  If "lag" is
		 * NULL then it's the first segment in the result.  Leave
		 * "lag" pointing at the end of the array.
		 */
		copy = json_array_flat(scan, depth - 1);
		if (lag)
			lag->next = copy->first;
		else
			result->first = copy->first;
		for (lag = copy->first; lag && lag->next; lag = lag->next) {
		}
		JSON_END_POINTER(result) = lag;

		/* Free the copy array node, but not its elements. */
		copy->first = NULL;
		json_free(copy);
	}

	/* Done! */
	return result;
}

/* Unroll nested tables (arrays of objects).  "table" should be the outer
 * table, and "nestlist" is an array of strings giving the names of the
 * nested arrays to unroll.  You may also intersperse boolean symbols to
 * control how to handle missing/empty tables -- false skips the whole
 * outer element, and true just keeps the outer element with nothing added
 * for inner rows.
 *
 * This always returns a table, except that in some situations it may return
 * an empty array which technically isn't a table.  The returned value is
 * COPIED from the input table; the original table is unchanged.
 */
json_t *json_unroll(json_t *table, json_t *nestlist)
{
	int	skipempty = 0;	/* If nested list is empty, do we skip it? */
	json_t	*value;		/* value of member to recursively unroll */
	json_t	*nested;	/* recursively-unrolled nested array */
	json_t	*nrow;		/* Used for scanning nested array's rows */
	json_t	*tmember;	/* Used for scanning table element's members */
	json_t	*nmember;	/* Used for scanning nrow object's members */
	json_t	*row;		/* Used for building a result element */
	json_t	*result;	/* Used to accumulate the result array */

	/* If not a table (including null!), just return an empty array */
	if (!table || (!json_is_table(table) && table->type != JSON_OBJECT))
		return json_array();

	/* We want to treat the nest list as linked list of JSON_STRINGs.
	 * Probably it comes to us as a JSON_ARRAY though; skip to the start
	 * of the first element of the array.  Skip any non-strings.  If we
	 * encounter a boolean, set the skipempty flag accordingly.
	 */
	if (nestlist && nestlist->type == JSON_ARRAY)
		nestlist = nestlist->first;
	while (nestlist && nestlist->type != JSON_STRING) {
		if (nestlist->type == JSON_BOOL)
			skipempty = json_is_true(nestlist);
		nestlist = nestlist->next;
	}

	/*  If nesting list is empty, return a copy of the table */
	if (!nestlist || (nestlist->type == JSON_ARRAY && !nestlist->first))
		return json_copy(table);

	/* Start with an empty response array */
	result = json_array();

	/* We expect the table argument to be a table.  No surprise there.
	 * But we also allow it to be a single object, which we treat as
	 * the only element in a one-element array.  This is partly to help
	 * work around bad XML conversions, and partly to add the flexibility
	 * to unroll tables within objects.
	 */
	if (table->type == JSON_ARRAY)
		table = table->first;
	else
		assert(table->type == JSON_OBJECT && !table->next);

	/* For each element of the table... */
	for (; table; table = table->next) {
		/* Fetch the unrolled nested variable */
		value = json_by_expr(table, nestlist->text, NULL);
		nested = json_unroll(value, nestlist->next);

		/* If nested is empty, either skip it or stuff an empty object
		 * into it.
		 */
		if (!nested->first) {
			if (skipempty) {
				json_free(nested);
				continue;
			}
			json_append(nested, json_object());
		}

		/* For each element of nested... */
		for (nrow = nested->first; nrow; nrow = nrow->next) {
			/* Create a new object which combines members of the
			 * table row and the current nested row.
			 */
			row = json_object();
			for (tmember = table->first; tmember; tmember = tmember->next) {
				/* Is this the unrolled element? */
				if (tmember->first == value) {
					/* Yes!  Replace it with copies of the
					 * nested object's members.  This is
					 * where the unrolling really happens.
					 */

					/* If this is the last row, we can
					 * recycle the members, but for other
					 * rows we need to make copies.
					 */
					if (nrow->next) {
						/* Append copies of the nested members */
						for (nmember = nrow->first; nmember; nmember = nmember->next)
							json_append(row, json_copy(nmember));
					} else {
						/* Last row, move nested members */
						json_t *next;
						for (nmember = nrow->first; nmember; nmember = next) {
							next = nmember->next;
							nmember->next = NULL;
							json_append(row, nmember);
						}
						nrow->first = NULL;/* so members won't be freed */
					}
				} else {
					/* Append a copy of this member */
					json_append(row, json_copy(tmember));
				}

			}

			/* Append the new row to the result array */
			json_append(result, row);
		}

		/* Clean up.  We're reusing nested's elements, but we can free
		 * nested itself (the JSON_ARRAY node) and its object shells
		 * (the JSON_OBJECT nodes).
		 */
		json_free(nested);
	}

	/* Return the result */
	return result;
}
