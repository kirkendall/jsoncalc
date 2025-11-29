#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jx.h>

/* Even if memory debugging is enabled, here we're defining the non-debugging
 * version of the jx_copy() and jx_copy_filter() functions.
 */
#ifdef JX_DEBUG_MEMORY
# undef jx_copy
# undef jx_copy_filter
#endif

/* Return a deep copy of a json object... meaning that if "json" is a container
 * then its contents are deep-copied too.  The returned object will be identical
 * to the "json" object, but altering one will have no effect on the other.
 */
jx_t *jx_copy_filter(jx_t *json, int (*test)(jx_t *elem))
{
	jx_t *copy;
	jx_t *tail = NULL;
	jx_t *scan;
	jx_t *sub;

	/* Defend against NULL */
	if (!json)
		return NULL;

	/* If there's a test, apply it to this item */
	if (test && !test(json))
		return NULL;

	/* The top node's copy method depends on its type */
	switch (json->type)
	{
	  case JX_OBJECT:
		copy = jx_object();
		for (tail = NULL, scan = json->first; scan; scan = scan->next) /* object */
		{
			sub = jx_copy_filter(scan, test);
			if (!sub)
				continue;
			if (tail)
				tail->next = sub; /* object */
			else
				copy->first = sub;
			tail = sub;
		}
		break;

	  case JX_ARRAY:
		copy = jx_array();

		/* For deferred arrays without a test, keep it deferred */
		if (jx_is_deferred_array(json) && !test) {
			jx_t basic;
			jxdef_t *def = (jxdef_t *)json->first;
			copy->first = jx_defer(def->fns);

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
		for (scan = jx_first(json); scan; scan = jx_next(scan))
		{
			sub = jx_copy_filter(scan, test);
			if (!sub)
				continue;
			jx_append(copy, sub);
		}
		break;

	  case JX_KEY:
		return jx_key(json->text, jx_copy(json->first));

	  case JX_STRING:
	  case JX_BOOLEAN:
	  case JX_NULL:
		return jx_simple(json->text, -1, json->type);

	  case JX_NUMBER:
		/* Numbers can be represented internally either as a string of
		 * ASCII digits copied directly from a JSON document, or in
		 * binary.  This affects the way we copy it.
		 */
		if (json->text[0])
			return jx_number(json->text, -1);
		if (json->text[1] == 'i')
			return jx_from_int(JX_INT(json));
		return jx_from_double(JX_DOUBLE(json));

	  case JX_DEFER:
	  default:
	  	return NULL; /* should never happen */
	}

	/* Return the whole array or object */
	return copy;
}

jx_t *jx_copy(jx_t *json)
{
	/* !!! It'd be nice if a deferred array was copied as deferred too */
	return jx_copy_filter(json, NULL);
}
