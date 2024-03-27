#include <stdlib.h>
#include <stdio.h>
#include "json.h"


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

	/* For each member of the array... */
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
