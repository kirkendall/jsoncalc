#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jx.h>
#include <assert.h>

/* This file defines functions useful for processing arrays, especially
 * deferred arrays.
 */

/* Return the first element of an array, or NULL if the array is empty.  You
 * can also safely call this on objects to iterate over the members, but
 * objects are never deferred.
 */
#ifdef JX_DEBUG_MEMORY
# undef jx_first
#endif
jx_t *jx_first(jx_t *arr)
{
	/* Defend against NULL */
	if (!arr)
		return NULL;

	/* If not an array, and not an element of an array (no "next") then
	 * return it though it was the only element of a single-element array.
	 * This is handy when processing data converted from XML, since XML
	 * doesn't really have arrays.
	 */
	if (arr->type != JX_ARRAY) {
		/* If a non-array is passed, and it isn't part of an array,
		 * then return it as though it was the only element in a
		 * one-element array.  Else NULL.
		 */
		if (!arr->next) /* object */
			return arr;
		return NULL;
	}

	/* Non-deferred arrays are easy */
	if (!jx_is_deferred_array(arr))
		return arr->first;

	/* Call the deferred array's "first" function */
	return (*((jxdef_t *)(arr->first))->fns->first)(arr);
}

/* Return the next element of an array, or NULL if there are no more elements.
 * You can also safely call this on object members to interate over the members,
 * but objects are never deferred.
 */
jx_t *jx_next(jx_t *elem)
{
	jx_t *result;

	/* Defend against NULL */
	if (!elem)
		return NULL;

	/* Non-deferred arrays are easy */
	if (!jx_is_deferred_element(elem))
		return elem->next; /* undeferred */

	/* Call the ->next function */
	result = (*((jxdef_t *)(elem->next))->fns->next)(elem);
	if (!result)
		jx_break(elem);
	return result;
}

/* Terminate a first/next loop prematurely.  This only matters when scanning
 * a deferred array.
 */
void jx_break(jx_t *elem)
{
	jxdef_t *def;

	/* If not deferred, do nothing */
	if (!jx_is_deferred_element(elem))
		return;

	/* Free the JX_DEFER node.  If it requires special freeing,
	 * do that first.
	 */
	def = (jxdef_t *)elem->next; /* deferred */
	if (def->fns->free)
		(*def->fns->free)(elem);
	jx_free(elem->next);
	elem->next = NULL;

	/* Free the element itself */
	jx_free(elem);
}

/* Test whether the current element is the last element in an array.  For
 * deferred arrays, this will NOT invalidate elem.
 */
int jx_is_last(const jx_t *elem)
{
	jxdef_t	*def;

	/* If not deferred, this is easy */
	if (!jx_is_deferred_element(elem))
		return elem->next ? 0 : 1; /* undeferred */

	/* Otherwise use the deferred type's islast() function */
	def = (jxdef_t *)elem->next;
	return (*def->fns->islast)(elem);
}

/* NOTE: The jx_is_deferred_array() and jx_is_deferred_element() functions
 * both test whether deferred array logic is necessary.  It's tempting to fold
 * these two functions together as a single jx_is_deferred() function but
 * that won't work for nested arrays -- you could have a deferred array of
 * undeferred arrays, and need to be able to classify a nested array separately
 * as deferred (element) and undeferred (array).
 */

/* Test whether a given array is deferred. */
int jx_is_deferred_array(const jx_t *arr)
{
	if (arr
	 && arr->type == JX_ARRAY
	 && arr->first
	 && arr->first->type == JX_DEFER)
		return 1;
	return 0;
}
/* Test whether a given item is an element of a deferred array. */
int jx_is_deferred_element(const jx_t *elem)
{
	if (elem && elem->next && elem->next->type == JX_DEFER) /* undeferred */
		return 1;
	return 0;
}

/* Convert a deferred file to undeferred.  The conversion is done in-place,
 * meaning the same "arr" node is used for the array, and arr->first will
 * simply point to the actual first element instead of a JX_DEFER node.
 */
void jx_undefer(jx_t *arr)
{
	jx_t	*undeferred, *scan;

	/* If already undeferred, just leave it */
	if (!jx_is_deferred_array(arr))
		return;

	/* Copy the elements into a new array */
	undeferred = jx_array();
	for (scan = jx_first(arr); scan; scan = jx_next(scan))
		jx_append(undeferred, jx_copy(scan));

	/* Replace the JX_DEFER node with the new array's contents */
	jx_free(arr->first);
	arr->first = undeferred->first;

	/* Clean up */
	undeferred->first = NULL;
	jx_free(undeferred);
}

/******************************************************************************/
/* The following implement the "..." operator as a deferred array. */

typedef struct {
	jxdef_t basic;/* normal stuff */
	int	from;	/* starting integer */
	int	to;	/* ending integer */
} jell_t;

static jx_t *jell_first(jx_t *defarray);
static jx_t *jell_next(jx_t *defelem);
static int jell_islast(const jx_t *defelem);
static jx_t *jell_byindex(jx_t *defarray, int idx);

static jxdeffns_t jell_fns = {
	/* size */	sizeof(jell_t),
	/* desc */	"Ellipsis",
	/* first */	jell_first,
	/* next */	jell_next,
	/* islast */	jell_islast,
	/* free */	NULL,
	/* byindex */	jell_byindex,
	/* bykey */	NULL
};

/* Return the first element */
static jx_t *jell_first(jx_t *defarray)
{
	jx_t *result;
	jell_t *jell;

	assert(defarray->first->type == JX_DEFER);
	jell = (jell_t *)defarray->first;

	/* Allocate an array for the first element */
	result = jx_from_int(jell->from);

	/* Because this is a deferred element, make it's ->next be JX_DEFER */
	result->next = jx_defer(&jell_fns);
	((jell_t *)result->next)->from = ((jell_t *)defarray->first)->from;
	((jell_t *)result->next)->to = ((jell_t *)defarray->first)->to;

	return result;
}

/* Move to the next element */
static jx_t *jell_next(jx_t *defelem)
{
	assert(defelem->next->type == JX_DEFER);
	assert(defelem->text[1] == 'i');

	/* Increment the value */
	JX_INT(defelem) ++;

	/* If it exceeds "to" then free it and return NULL */
	if (JX_INT(defelem) > ((jell_t *)defelem->next)->to)
		return NULL;

	return defelem;
}

/* Test whether the current element is the last element */
static int jell_islast(const jx_t *defelem)
{
	assert(defelem->next->type == JX_DEFER);
	assert(defelem->text[1] == 'i');

	/* If it exceeds "to" then free it and return NULL */
	return (JX_INT(defelem) >= ((jell_t *)defelem->next)->to);
}

static jx_t *jell_byindex(jx_t *defarray, int idx)
{
	jell_t	*jell, *jell2;
	jx_t	*result;
	assert(defarray->type == JX_ARRAY && defarray->first && defarray->first->type == JX_DEFER);

	/* If outside of array bounds, return NULL */
	jell = (jell_t *)(defarray->first);
	if (idx < 0 || idx >= jx_length(defarray))
		return NULL;

	/* Allocate a new jx_t to store the element.  Mark it as deferred */
	result = jx_from_int(jell->from + idx);
	result->next = jx_defer(&jell_fns);
	jell2 = (jell_t *)(defarray->first);
	jell2->from = jell->from;
	jell2->to = jell->to;

	/* Return it. */
	return result;
}


/* Allocate a deferred array for a range of integers */
jx_t *jx_defer_ellipsis(int from, int to)
{
	jx_t	*result;

	/* Allocate a normal array */
	result = jx_array();

	/* If from > to then we're done!  Nothing to defer */
	if (from > to)
		return result;

	/* Allocate a jell_t as ->first */
	result->first = jx_defer(&jell_fns);

	/* Store the integer range */
	((jell_t *)result->first)->from = from;
	((jell_t *)result->first)->to = to;

	/* Store the size */
	result->text[1] = 'n'; /* not a table, just an array of integers */
	JX_ARRAY_LENGTH(result) = to - from + 1;

	return result;
}
