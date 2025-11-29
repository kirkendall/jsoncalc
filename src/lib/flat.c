#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <jx.h>


/* Return a new array which merges any embedded arrays into new array proper.
 * For example, [1,[2,3],4] would become [1,2,3,4].  Depth can be 0 to just
 * copy without changing anything, 1 for a single layer, 2 for 2 layers deep,
 * or as a special case, -1 means unlimited depth.
 */
jx_t *jx_array_flat(jx_t *array, int depth)
{
	jx_t *result;
	jx_t *scan, *lag;
	jx_t *copy;

	/* Defend against NULL */
	if (!array)
		return NULL;

	/* If not an array, return NULL.  (We don't dare return it unchanged
	 * because this function is intended to return a NEW array, not a
	 * link to existing data.)
	 */
	if (array->type != JX_ARRAY)
		return NULL;

	/* Start a new array */
	result = jx_array();

	/* For each element of the array... */
	for (lag = NULL, scan = array->first; scan; scan = scan->next) { /* undeferred */
		/* If depth is 0 or this element isn't array, copy it */
		if (depth == 0 || scan->type != JX_ARRAY) {
			lag = jx_copy(scan);
			jx_append(result, lag);
			continue;
		}

		/* Okay, we have an array and we want to include it.  Copy it
		 * via a recursive call to jx_array_flat(), and then munge
		 * the pointers to make it appear after "lag".  If "lag" is
		 * NULL then it's the first segment in the result.  Leave
		 * "lag" pointing at the end of the array.
		 */
		copy = jx_array_flat(scan, depth - 1);
		if (lag)
			lag->next = copy->first; /* undeferred */
		else
			result->first = copy->first;
		for (lag = copy->first; lag && lag->next; lag = lag->next) { /* undeferred */
		}
		JX_END_POINTER(result) = lag;

		/* Free the copy array node, but not its elements. */
		copy->first = NULL;
		jx_free(copy);
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
jx_t *jx_unroll(jx_t *table, jx_t *nestlist)
{
	int	skipempty = 0;	/* If nested list is empty, do we skip it? */
	jx_t	*value;		/* value of member to recursively unroll */
	jx_t	*nested;	/* recursively-unrolled nested array */
	jx_t	*nrow;		/* Used for scanning nested array's rows */
	jx_t	*tmember;	/* Used for scanning table element's members */
	jx_t	*nmember;	/* Used for scanning nrow object's members */
	jx_t	*row;		/* Used for building a result element */
	jx_t	*result;	/* Used to accumulate the result array */

	/* If not a table (including null!), just return an empty array */
	if (!table || (!jx_is_table(table) && table->type != JX_OBJECT))
		return jx_array();

	/* This won't work for deferred arrays */
	jx_undefer(table);

	/* We want to treat the nest list as linked list of JX_STRINGs.
	 * Probably it comes to us as a JX_ARRAY though; skip to the start
	 * of the first element of the array.  Skip any non-strings.  If we
	 * encounter a boolean, set the skipempty flag accordingly.
	 */
	if (nestlist && nestlist->type == JX_ARRAY)
		nestlist = nestlist->first; /* undeferred */
	while (nestlist && nestlist->type != JX_STRING) {
		if (nestlist->type == JX_BOOLEAN)
			skipempty = jx_is_true(nestlist);
		nestlist = nestlist->next; /* undeferred */
	}

	/*  If nesting list is empty, return a copy of the table */
	if (!nestlist || (nestlist->type == JX_ARRAY && !nestlist->first))
		return jx_copy(table);

	/* Start with an empty response array */
	result = jx_array();

	/* For each element of the table... */
	for (table = jx_first(table); table; table = jx_next(table)) {
		/* Fetch the unrolled nested variable */
		value = jx_by_expr(table, nestlist->text, NULL); /* undeferred */
		nested = jx_unroll(value, nestlist->next);

		/* If nested is empty, either skip it or stuff an empty object
		 * into it.
		 */
		if (!nested->first) {
			if (skipempty) {
				jx_free(nested);
				continue;
			}
			jx_append(nested, jx_object());
		}

		/* For each element of nested... */
		for (nrow = jx_first(nested); nrow; nrow = jx_next(nrow)) {
			/* Create a new object which combines members of the
			 * table row and the current nested row.
			 */
			row = jx_object();
			for (tmember = table->first; tmember; tmember = tmember->next) { /* object */
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
					if (!jx_is_last(nrow) || jx_is_deferred_element(nrow)) {
						/* Append copies of the nested members */
						for (nmember = nrow->first; nmember; nmember = nmember->next) /* object */
							jx_append(row, jx_copy(nmember));
					} else {
						/* Last row, move nested members */
						jx_t *next;
						for (nmember = nrow->first; nmember; nmember = next) {
							next = nmember->next; /* object */
							nmember->next = NULL; /* object */
							jx_append(row, nmember);
						}
						nrow->first = NULL;/* so members won't be freed */
					}
				} else {
					/* Append a copy of this member */
					jx_append(row, jx_copy(tmember));
				}

			}

			/* Append the new row to the result array */
			jx_append(result, row);
		}

		/* Clean up.  We're reusing nested's elements, but we can free
		 * nested itself (the JX_ARRAY node) and its object shells
		 * (the JX_OBJECT nodes).
		 */
		jx_free(nested);
	}

	/* Return the result */
	return result;
}
