#include <stdlib.h>
#include <stdio.h>
#include <jsoncalc.h>


/* Given a table (array of objects) and a list of member names for grouping
 * the objects (array of strings), return a new array which contains a nested
 * array for each group.
 */
json_t *json_array_group_by(json_t *array, json_t *orderby)
{
	json_t *result, *group;
	json_t *scan;

	/* Defend against NULL */
	if (!array)
		return NULL;

	/* If not a table, return it unchanged */
	if (!json_is_table(array))
		return array;

	/* Start a new array */
	result = json_array();

	/* For each member of the array... */
	for (scan = array->first, group = NULL; scan; scan = scan->next) {
		/* New group? */
		if (!group || json_compare(group->first, scan, orderby) != 0) {
			/* Start the group */
			group = json_array();
			json_append(result, group);
		}

		/* Add this element to the current group */
		json_append(group, json_copy(scan));
	}

	/* Done! */
	return result;
}
