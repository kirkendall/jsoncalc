#include <stdlib.h>
#include <stdio.h>
#include <jsoncalc.h>

/* Even if memory debugging is enabled, here we're defining the non-debugging verion
 * of the json_copy() function.
 */
#ifdef JSON_DEBUG_MEMORY
# undef json_copy
# undef json_copy_filter
#endif

/* Return a deep copy of a json object... meaning that if "json" is a container
 * then its contents are deep-copied too.  The returned object will be identical
 * to the "json" object, but altering one will have no effect on the other.
 */
json_t *json_copy_filter(json_t *json, int (*test)(json_t *elem))
{
	json_t *copy;
	json_t *tail = NULL;
	json_t *scan;
	json_t *sub;

	/* Defend against NULL */
	if (!json)
		return NULL;

	/* If there's a test, apply it to this item */
	if (test && !test(json))
		return NULL;

	/* The top node's copy method depends on its type */
	switch (json->type)
	{
	  case JSON_OBJECT:
		copy = json_object();
		break;

	  case JSON_ARRAY:
		copy = json_array();
		break;

	  case JSON_KEY:
		return json_key(json->text, json_copy(json->first));

	  case JSON_STRING:
	  case JSON_BOOL:
	  case JSON_NULL:
		return json_simple(json->text, -1, json->type);

	  case JSON_NUMBER:
		/* Numbers can be represented internally either as a string of
		 * ASCII digits copied directly from a JSON document, or in
		 * binary.  This affects the way we copy it.
		 */
		if (json->text[0])
			return json_number(json->text, -1);
		if (json->text[1] == 'i')
			return json_from_int(JSON_INT(json));
		return json_from_double(JSON_DOUBLE(json));

	  default:
	  	return NULL; /* should never happen */
	}

	/* We only get here for arrays and objects, either of which could have
	 * a long list of elements/members to copy.  We use iteration to find
	 * them, and recursion to copy them.
	 */
	for (tail = NULL, scan = json->first; scan; scan = scan->next)
	{
		sub = json_copy_filter(scan, test);
		if (!sub)
			continue;
		if (tail)
		{
			tail->next = sub;
			tail = tail->next;
		} else {
			tail = copy->first = sub;
		}
	}

	/* Return the whole array or object */
	return copy;
}

json_t *json_copy(json_t *json)
{
	return json_copy_filter(json, NULL);
}
