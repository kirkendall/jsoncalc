#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jsoncalc.h>
#include <assert.h>

/* This file defines functions useful for processing arrays, especially
 * deferred arrays.
 */

/* Return the first element of an array, or NULL if the array is empty.  You
 * can also safely call this on objects to iterate over the members, but
 * objects are never deferred.
 */
json_t *json_first(json_t *arr)
{
	/* If not an array, and not an element of an array (no "next") then
	 * return it though it was the only element of a single-element array.
	 * This is handy when processing data converted from XML, since XML
	 * doesn't really have arrays.
	 */
	if (arr->type != JSON_ARRAY) {
		/* If a non-array is passed, and it isn't part of an array,
		 * then * return it as though it was the only element in a
		 * one-element array.  Else NULL.
		 */
		if (!arr->next) /* object */
			return arr;
		return NULL;
	}

	/* Non-deferred arrays are easy */
	if (!json_is_deferred_array(arr))
		return arr->first;

	/* Call the deferred array's "first" function */
	return (*((jsondef_t *)(arr->first))->fns->first)(arr);
}

/* Return the next element of an array, or NULL if there are no more elements.
 * You can also safely call this on object members to interate over the members,
 * but objects are never deferred.
 */
json_t *json_next(json_t *elem)
{
	json_t *result;

	/* Non-deferred arrays are easy */
	if (!json_is_deferred_element(elem))
		return elem->next; /* undeferred */

	/* Call the ->next function */
	result =  (*((jsondef_t *)(elem->next))->fns->next)(elem);
	if (!result)
		json_free(elem);
	return result;
}

/* Terminate a first/next loop prematurely.  This only matters when scanning
 * a deferred array.
 */
void json_break(json_t *elem)
{
	/* If not deferred, do nothing */
	if (!json_is_deferred_element(elem))
		return;

	/* Free it.  If it requires special freeing, do that first */
	json_free(elem);
}

/* Test whether the current element is the last element in an array.  For
 * deferred arrays, this will NOT invalidate elem.
 */
int json_is_last(const json_t *elem)
{
	jsondef_t	*def;

	/* If not deferred, this is easy */
	if (!json_is_deferred_element(elem))
		return elem->next ? 0 : 1; /* undeferred */

	/* Otherwise use the deferred type's islast() function */
	def = (jsondef_t *)elem->next;
	return (*def->fns->islast)(elem);
}

/* NOTE: The json_is_deferred_array() and json_is_deferred_element() functions
 * both test whether deferred array logic is necessary.  It's tempting to fold
 * these two functions together as a single json_is_deferred() function but
 * that won't work for nested arrays -- you could have a deferred array of
 * undeferred arrays, and need to be able to classify a nested array separately
 * as deferred (element) and undeferred (array).
 */

/* Test whether a given array is deferred. */
int json_is_deferred_array(const json_t *arr)
{
	if (arr
	 && arr->type == JSON_ARRAY
	 && arr->first
	 && arr->first->type == JSON_DEFER)
		return 1;
	return 0;
}
/* Test whether a given item is an element of a deferred array. */
int json_is_deferred_element(const json_t *elem)
{
	if (elem && elem->next && elem->next->type == JSON_DEFER) /* undeferred */
		return 1;
	return 0;
}

/* Convert a deferred file to undeferred.  The conversion is done in-place,
 * meaning the same "arr" node is used for the array, and arr->first will
 * simply point to the actual first element instead of a JSON_DEFER node.
 */
void json_undefer(json_t *arr)
{
	json_t	*undeferred, *scan;

	/* If already undeferred, just leave it */
	if (!json_is_deferred_array(arr))
		return;

	/* Copy the elements into a new array */
	undeferred = json_array();
	for (scan = json_first(arr); scan; scan = json_next(scan))
		json_append(undeferred, json_copy(scan));

	/* Replace the JSON_DEFER node with the new array's contents */
	json_free(arr->first);
	arr->first = undeferred->first;

	/* Clean up */
	undeferred->first = NULL;
	json_free(undeferred);
}

/******************************************************************************/
/* The following implement the "..." operator as a deferred array. */

typedef struct {
	jsondef_t basic;/* normal stuff */
	int	from;	/* starting integer */
	int	to;	/* ending integer */
} jell_t;

static json_t *jell_first(json_t *defarray);
static json_t *jell_next(json_t *defelem);
static int jell_islast(const json_t *defelem);
static json_t *jell_byindex(json_t *defarray, int idx);

static jsondeffns_t jell_fns = {
	/* size */	sizeof(jell_t),
	/* desc */	"ellipsis",
	/* first */	jell_first,
	/* next */	jell_next,
	/* islast */	jell_islast,
	/* free */	NULL,
	/* byindex */	jell_byindex,
	/* bykey */	NULL
};

/* Return the first element */
static json_t *jell_first(json_t *defarray)
{
	json_t *result;
	jell_t *jell;

	assert(defarray->first->type == JSON_DEFER);
	jell = (jell_t *)defarray->first;

	/* Allocate an array for the first element */
	result = json_from_int(jell->from);

	/* Because this is a deferred element, make it's ->next be JSON_DEFER */
	result->next = json_defer(&jell_fns);
	((jell_t *)result->next)->from = ((jell_t *)defarray->first)->from;
	((jell_t *)result->next)->to = ((jell_t *)defarray->first)->to;

	return result;
}

/* Move to the next element */
static json_t *jell_next(json_t *defelem)
{
	assert(defelem->next->type == JSON_DEFER);
	assert(defelem->text[1] == 'i');

	/* Increment the value */
	JSON_INT(defelem) ++;

	/* If it exceeds "to" then free it and return NULL */
	if (JSON_INT(defelem) > ((jell_t *)defelem->next)->to)
		return NULL;

	return defelem;
}

/* Test whether the current element is the last element */
static int jell_islast(const json_t *defelem)
{
	assert(defelem->next->type == JSON_DEFER);
	assert(defelem->text[1] == 'i');

	/* If it exceeds "to" then free it and return NULL */
	return (JSON_INT(defelem) >= ((jell_t *)defelem->next)->to);
}

static json_t *jell_byindex(json_t *defarray, int idx)
{
	jell_t	*jell, *jell2;
	json_t	*result;
	assert(defarray->type == JSON_ARRAY && defarray->first && defarray->first->type == JSON_DEFER);

	/* If outside of array bounds, return NULL */
	jell = (jell_t *)(defarray->first);
	if (idx < 0 || idx >= json_length(defarray))
		return NULL;

	/* Allocate a new json_t to store the element.  Mark it as deferred */
	result = json_from_int(jell->from + idx);
	result->next = json_defer(&jell_fns);
	jell2 = (jell_t *)(defarray->first);
	jell2->from = jell->from;
	jell2->to = jell->to;

	/* Return it. */
	return result;
}


/* Allocate a deferred array for a range of integers */
json_t *json_defer_ellipsis(int from, int to)
{
	json_t	*result;

	/* Allocate a normal array */
	result = json_array();

	/* If from > to then we're done!  Nothing to defer */
	if (from > to)
		return result;

	/* Allocate a jell_t as ->first */
	result->first = json_defer(&jell_fns);

	/* Store the integer range */
	((jell_t *)result->first)->from = from;
	((jell_t *)result->first)->to = to;

	/* Store the size */
	result->text[1] = 'n'; /* not a table, just an array of integers */
	JSON_ARRAY_LENGTH(result) = to - from + 1;

	return result;
}
