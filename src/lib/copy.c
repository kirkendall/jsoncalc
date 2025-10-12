#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
		for (tail = NULL, scan = json->first; scan; scan = scan->next) /* object */
		{
			sub = json_copy_filter(scan, test);
			if (!sub)
				continue;
			if (tail)
				tail->next = sub; /* object */
			else
				copy->first = sub;
			tail = sub;
		}
		break;

	  case JSON_ARRAY:
		copy = json_array();

		/* For deferred arrays without a test, keep it deferred */
		if (json_is_deferred_array(json) && !test) {
			json_t basic;
			jsondef_t *def = (jsondef_t *)json->first;
			copy->first = json_defer(def->fns);

			/* We want to copy all data associated with this
			 * deferred array, except for "basic".  We keep
			 * "basic" separate so its memory tracking is
			 * independent.
			 */
			basic = *copy->first;
			memcpy(copy->first, json->first, def->fns->size);
			*copy->first = basic;

			/* If a file is referenced, this is a new reference */
			if (def->file)
				def->file->refs++;
			break;
		}

		/* Otherwise we scan, filter, and copy elements individually */
		for (scan = json_first(json); scan; scan = json_next(scan))
		{
			sub = json_copy_filter(scan, test);
			if (!sub)
				continue;
			json_append(copy, sub);
		}
		break;

	  case JSON_KEY:
		return json_key(json->text, json_copy(json->first));

	  case JSON_STRING:
	  case JSON_BOOLEAN:
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

	  case JSON_DEFER:
	  default:
	  	return NULL; /* should never happen */
	}

	/* Return the whole array or object */
	return copy;
}

json_t *json_copy(json_t *json)
{
	/* !!! It'd be nice if a deferred array was copied as deferred too */
	return json_copy_filter(json, NULL);
}
