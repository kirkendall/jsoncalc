#include <stdlib.h>
#include <stdio.h>
#include "json.h"

/* Sort a JSON array in place, given a list of fields. */
void json_sort(json_t *array, json_t *orderby)
{
	json_t *sorted;

	// Check parameters.
	if (array->type != JSON_ARRAY)
		json_throw(NULL, "json_sort() should be passed an array");
	if (orderby->type != JSON_ARRAY)
		json_throw(NULL, "The field list passed to json_sort() must be an array");
	if (!array->first)
		return; /* An empty array is always sorted.  No change needed. */
	if (!json_is_table(array))
		json_throw(NULL, "json_sort() should be passed an array of objects");

	/* We'll use a backwards insertion sort.  We'll scan for the highest
	 * record, remove it from the array, and add it to a new list.  We
	 * keep doing that until the original array is empty.
	 */
	sorted = NULL;
	while (array->first) {
		/* Start by assuming the first is the highest */
		json_t *highest = array->first;
		json_t *before_highest = NULL;
		json_t *scan, *lag;

		/* Scan all others, comparing them to highest-so-far.
		 * If we find a new highest-so-far, remember it.
		 */
		for (lag = array->first, scan = array->first->next; scan; lag = scan, scan = scan->next) {
			if (json_compare(highest, scan, orderby) < 0) {
				highest = scan;
				before_highest = lag;
			}
		}

		/* Remove the highest item from the array */
		if (before_highest)
			before_highest->next = highest->next;
		else
			array->first = highest->next;

		/* Insert it to the front of the sorted list */
		highest->next = sorted;
		sorted = highest;
	}

	/* Store the sorted list in the array again */
	array->first = sorted;

	/* This may have changed the end pointer for the array */
	JSON_END_POINTER(array) = NULL;
}
