#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "json.h"

/* Elements with the same value in a sort field are collected in a linked list.
 * "arraybuf" is the head of that list, and "value" points to the member that
 * we're sorting by.
 */
typedef struct {
	json_t	arraybuf;
	json_t	*value;
	double	dvalue;
} bucket_t;

/* Compare the sorting values for two buckets */
static int cmpascending(const void *v1, const void *v2)
{
	bucket_t *b1 = (bucket_t *)v1;
	bucket_t *b2 = (bucket_t *)v2;

	/* object/array/null/missing comes LAST */
	if (b1->value && !b2->value)
		return -1;
	if (b2->value && !b1->value)
		return 1;
	if (!b2->value)
		return 0;

	/* Booleans before numbers or strings */
	if (b1->value->type == JSON_BOOL && b2->value->type == JSON_BOOL)
		return strcmp(b1->value->text, b2->value->text);
	if (b1->value->type == JSON_BOOL || b2->value->type == JSON_BOOL)
		return -1;

	/* Strings before numbers */
	if (b1->value->type == JSON_STRING && b2->value->type == JSON_STRING)
		return json_mbs_casecmp(b1->value->text, b2->value->text);
	if (b1->value->type == JSON_STRING)
		return -1;
	if (b2->value->type == JSON_STRING)
		return 1;

	/* Numbers */
	if (b1->dvalue < b2->dvalue)
		return -1;
	if (b1->dvalue > b2->dvalue)
		return 1;
	return 0;
}

/* Descending version of sort comparison.  We just swap arguments. */
static int cmpdescending(const void *v1, const void *v2)
{
	return cmpascending(v2, v1);
}


/* This helper function does the real sorting, after parameters have been
 * checked.
 */
static void jcsort(json_t *array, json_t *orderby)
{
	json_t	*elem, *value;
	int	descending;
	int	nbuckets, used, b, b2;
	bucket_t *bucket;
	double	dvalue;

	descending = 0;
	if (orderby && orderby->type == JSON_BOOL) {
		descending = json_is_true(orderby);
		orderby = orderby->next;
	}

	/* Start with an empty bucket array */
	nbuckets = used = 0;
	bucket = NULL;

	/* Split the array elements out to buckets.  For strings, we do this
	 * in a case-sensitive way at this phase.
	 */
	while (array->first) {
		/* If user aborted, then quit */
		if (json_interupt)
			return;

		/* Pull the element out of the array */
		elem = array->first;
		array->first = elem->next;
		elem->next = NULL;

		/* Fetch its sort value. */
		value = json_by_expr(elem, orderby->text, NULL);
		if (value && value->type == JSON_NUMBER)
			dvalue = json_double(value);

		/* Find a bucket for this value. */
		for (b = 0; b < used; b++) {
			if (!value && !bucket[b].value)
				break;
			else if (value
			      && bucket[b].value
			      && (value->type == JSON_STRING || value->type == JSON_BOOL)
			      && bucket[b].value->type == value->type) {
				if (!strcmp(value->text, bucket[b].value->text))
					break;
			} else if (value
			      && value->type == JSON_NUMBER
			      && bucket[b].value->type == JSON_NUMBER) {
				if (dvalue == bucket[b].dvalue)
					break;
			} else if (!bucket[b].value)
				break; /* so arrays/objects/null all share a bucket */
		}

		/* If no bucket was found, allocate one */
		if (b >= used) {
			/* Maybe enlarge the bucket array */
			if (used == nbuckets) {
				nbuckets = used * 3 / 2 + 100;
				bucket = (bucket_t *)realloc(bucket, nbuckets * sizeof(bucket_t));
				memset(&bucket[used], 0, (nbuckets - used) * sizeof(bucket_t));
				for (b2 = used; b2 < nbuckets; b2++)
					bucket[b2].arraybuf.type = JSON_ARRAY;
			}

			/* Set its value */
			bucket[b].value = value;
			if (value && value->type == JSON_NUMBER)
				bucket[b].dvalue = dvalue;
			used++;
		}

		/* Add this element to the bucket */
		json_append(&bucket[b].arraybuf, elem);
	}

	/* Sort the buckets */
	if (descending)
		qsort(bucket, used, sizeof(bucket_t), cmpdescending);
	else
		qsort(bucket, used, sizeof(bucket_t), cmpascending);

	/* Merge any buckets that have the same case-insensitive value */
	for (b = 0, b2 = 1; b2 < used; b2++) {
		if (bucket[b].value
		 && bucket[b].value->type == JSON_STRING
		 && bucket[b2].value
		 && bucket[b2].value->type == JSON_STRING
		 && !json_mbs_casecmp(bucket[b].value->text, bucket[b2].value->text)) {
			/* Same, case-insenstively.  Append b2 to b */
			JSON_END_POINTER(&bucket[b].arraybuf)->next = bucket[b].arraybuf.first;
			JSON_END_POINTER(&bucket[b].arraybuf) = JSON_END_POINTER(&bucket[b2].arraybuf);
		} else {
			b++;
		}
	}
	used = b + 1;

	/* If there are more sort keys, then recursively sort each bucket */
	if (orderby->next) {
		for (b = 0; b < used; b++)
			jcsort(&bucket[b].arraybuf, orderby->next);
	}

	/* Merge the buckets back into the array again */
	array->first = bucket[0].arraybuf.first;
	JSON_END_POINTER(array) = JSON_END_POINTER(&bucket[0].arraybuf);
	for (b = 1; b < used; b++) {
		JSON_END_POINTER(array)->next = bucket[b].arraybuf.first;
		JSON_END_POINTER(array) = JSON_END_POINTER(&bucket[b].arraybuf);
	}

	/* Clean up */
	free(bucket);
}


/* Sort a JSON table (array of objects) in place, given a list of fields.
 * The orderby list should be an array of strings; you may also include
 * a boolean "true" before any field name to make it use descending sort.
 */
void json_sort(json_t *array, json_t *orderby)
{
	json_t	*check;
	int	anykeys;

	/* Check parameters. "array" must be a table, and "orderby" should
	 * be a list (array, or linked list of elements from an array) of
	 * field names and descending flags.
	 */
	if (!json_is_table(array)) {
		/* EEE "json_sort() should be passed an array of objects" */
		return;
	}
	if (orderby->type == JSON_ARRAY)
		orderby = orderby->first;
	anykeys = 0;
	for (check = orderby; check; check = check->next) {
		if (orderby->type == JSON_STRING)
			anykeys++;
		else if (orderby->type != JSON_BOOL) {
			/* EEE json_sort() key list must be strings and booleans */
			return;
		}
		else if (!orderby->next) {
			/* EEE json_sort() key list can't end with a boolean */
			return;
		}
	}
	if (!anykeys) {
		/* EEE Empty orderby list */
		return;
	}

	/* An empty array is always sorted.  So is a 1-element array. */
	if (!array->first || !array->first->next)
		return;

	/* Do the real sort */
	jcsort(array, orderby);
}
