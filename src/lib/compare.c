#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jsoncalc.h>

/* Compare two objects, given a list of keys.  Return 1 if the first object
 * compares as higher, -1 if the second object compares as higher, or 0 if
 * they compare as equal.  orderby is a list of keys names (array of strings);
 * to select descending order for any key, insert a true symbol into the
 * list before the key string.
 */
int json_compare(json_t *obj1, json_t *obj2, json_t *orderby)
{
	json_t *field1, *field2, *key;
	int	descending;
	double	diff;

	// Check parameters.
	if (obj1->type != JSON_OBJECT || obj2->type != JSON_OBJECT)
		;/* EEE "Records passed to json_compare() must be objects" */
	if (orderby->type != JSON_ARRAY && orderby->type != JSON_STRING)
		;/* EEE "The field list passed to json_compare() must be an array or a string" */

	// For each field...
	descending = 0;
	if (orderby->type == JSON_ARRAY)
		key = orderby->first;
	else
		key = orderby;
	for (;key; key = key->next) { /* object */
		// If this is a "descending" flag, then just do that
		if (key->type == JSON_BOOL) {
			descending = json_is_true(key);
			continue;
		}

		/* Silently ignore non-strings */
		if (key->type != JSON_STRING)
			continue;

		/* Get the members for the next key */
		field1 = json_by_expr(obj1, key->text, NULL);
		field2 = json_by_expr(obj2, key->text, NULL);
		if (json_is_null(field1) && json_is_null(field2))
			continue; // if both are NULL, skip it
		if (json_is_null(field1))
			return 1; // NULL comes after non-NULL
		if (json_is_null(field2))
			return -1; // NULL comes after non-NULL

		// Compare, based on type.  Assume both are same type
		if (field1->type == JSON_NUMBER)
			diff = json_double(field1) - json_double(field2);
		else
			diff = strcasecmp(field1->text, field2->text);

		// If descending then invert
		if (descending) {
			diff = -diff;
			descending = !descending;
		}

		// If equal then skip to the next field
		if (diff == 0)
			continue;

		// Return only -1, 1
		return (diff < 0) ? -1 : 1;
	}

	// We reached the end without finding any difference
	return 0;
}
