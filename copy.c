#include <stdlib.h>
#include <stdio.h>
#include "json.h"


/* Return a deep copy of a json object... meaning that if "json" is a container
 * then its contents are deep-copied too.  The returned object will be identical
 * to the "json" object, but altering one will have no effect on the other.
 */
json_t *json_copy(json_t *json)
{
	json_t *copy;
	json_t *tail = NULL;
	json_t *scan;

	/* Defend against NULL */
	if (!json)
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
	  case JSON_SYMBOL:
		return json_simple(json->text, -1, json->type);

	  case JSON_NUMBER:
		/* Numbers can be represented internally either as a string of ASCII
		 * digits copied directly from a JSON document, or in binary.  This
		 * affects the way we copy it.
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
		if (tail)
		{
			tail->next = json_copy(scan);
			tail = tail->next;
		} else
			tail = copy->first = json_copy(scan);
	}

	/* Return the whole array or object */
	return copy;
}
