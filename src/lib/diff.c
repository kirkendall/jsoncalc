#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <jx.h>

/* Comparison function for sorting keys */
static int cmp_keys(const void *p1, const void *p2) {
	return strcmp((*(jx_t **)p1)->text, (*(jx_t **)p2)->text);
}

/* Given an object, return a sorted array of its member JX_KEY nodes.  The
 * end of the array will be indicated by a NULL pointer.
 */
static jx_t **sort_keys(jx_t *obj)
{
	jx_t	*mem;
	int	i;
	int	len = jx_length(obj);
	jx_t	**sorted = malloc((len + 1) * sizeof(jx_t*));
	for (mem = obj->first, i = 0; mem; mem = mem->next, i++)
		sorted[i] = mem;
	sorted[i] = NULL;
	qsort(sorted, len, sizeof(jx_t *), cmp_keys);
	return sorted;
}

/* This basically computes a fletcher-style checksum of the words in a jx_t.
 * If it is an array, object, or key then the contents are also added to the
 * sum.  You should pass 0 for the seed.
 */
int jx_hash(jx_t *json, int seed)
{
	int	hash = seed;
	uint16_t sum = hash & 0xffff;
	uint16_t sumsum = (hash >> 16 & 0xffff);
	size_t len, pos;
	jx_t tmpbuf, *scan;

	/* Always add the type */
	sum += json->type;
	sumsum += sum;

	/* Type-dependent additions */
	if (json->type == JX_STRING || json->type == JX_KEY || (json->type == JX_OBJECT && *json->text)) {
		/* For strings and keys, add the full text.  Also, some day I
		 * expect to support object classes where the class name is
		 * stored in ->text so add that too.
		 */
		len = strlen(json->text);
		for (pos = 0; pos < len; pos += 2) {
			sum += *(uint16_t*)(json->text + pos);
			sumsum += sum;
		}
	}
	else if (json->type == JX_BOOLEAN) {
		/* For booleans, add the "t" or "f" */
		sum += json->type;
		sumsum += sum;
	} else if (json->type == JX_NUMBER) {
		/* Numbers can be textual or binary, but we want them to
		 * compare the same either way.  If textual, convert to
		 * binary.
		 */
		if (*json->text) {
			/* Textual. Is it integer or float? */
			pos = 0;
			if (json->text[0] == '-')
				pos++;
			for (; isdigit(json->text[pos]); pos++) {
			}

			/* Convert to the appropriate type of binary */
			memset(&tmpbuf, 0, sizeof tmpbuf);
			if (json->text[pos]) {
				JX_DOUBLE(&tmpbuf) = atof(json->text);
				tmpbuf.text[1] = 'd';
			} else {
				JX_INT(&tmpbuf) = atoi(json->text);
				tmpbuf.text[1] = 'i';
			}

			/* Sum up the words in ->text */
			len = sizeof(tmpbuf.text);
			for (pos = 0; pos < len; pos += 2) {
				sum += *(uint16_t*)(tmpbuf.text + pos);
				sumsum += sum;
			}
		} else {
			/* Binary */
			len = sizeof(json->text);
			for (pos = 0; pos < len; pos += 2) {
				sum += *(uint16_t*)(json->text + pos);
				sumsum += sum;
			}
		}
	}

	/* Combine sum and sumsum back into a single value */
	hash = (sumsum << 16) | sum;

	/* Any embedded content? */
	if (json->first) {
		/* For keys, we can recurse over ->first */
		if (json->type == JX_KEY)
			hash = jx_hash(json->first, hash);

		/* For arrays, we loop over the elements and incorporate their
		 * sums.
		 */
		if (json->type == JX_ARRAY) {
			for (scan = jx_first(json); scan; scan = jx_next(scan))
				hash = jx_hash(scan, hash);
		}

		/* For objects, we loop over the members.  HOWEVER, differences
		 * in the order of keys shouldn't matter, so first we want to
		 * sort them and then generate the sums in order.
		 */
		if (json->type == JX_OBJECT) {
			/* Build a sorted list of members */
			int i;
			jx_t **sorted = sort_keys(json);

			/* Incorporate the sums for each key (and its value) */
			for (i = 0; sorted[i]; i++)
				hash = jx_hash(sorted[i], hash);

			/* Free the sorted list */
			free(sorted);
		}
	}

	/* Return the sum */
	return hash;
}

/* Search for differences between two arrays */
static jx_t *jx_diff_arrays(jx_t *jxold, jx_t *jxnew)
{
	return jx_array();
}

/* Search for differences between two objects */
static jx_t *jx_diff_objects(jx_t *jxold, jx_t *jxnew)
{
	return jx_array();
}

jx_t *jx_diff(jx_t *jxold, jx_t *jxnew)
{
	if (jxold->type == JX_ARRAY && jxnew->type == JX_ARRAY)
		return jx_diff_arrays(jxold, jxnew);
	else if (jxold->type == JX_OBJECT && jxnew->type == JX_OBJECT)
		return jx_diff_objects(jxold, jxnew);
	else
		return jx_error_null(NULL, "difftypes:Bad mix of types for diff");
}
