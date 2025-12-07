#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#define __USE_XOPEN
#define _XOPEN_SOURCE
#include <wchar.h>
#include <jx.h>

/* This stores an an element's hash value, when diffing arrays.  When diffing
 * objects, it also stores the member's key which is important because for
 * objects we need to store the members by key since member order doesn't
 * matter.
 */
typedef struct {
	jx_t	*key;	/* For objects, this is the key; for arrays, ignored */
	int	hash;	/* Hash value of an element */
} jxhash_t;

typedef struct {
	jx_t	*json;
	jxhash_t *array;
	int	len;
	int	cur;
} jxcolumn_t;

/* This stores info to guide the diff, including the two sets of data to
 * compare, how to represent mismatches, and where to accumulate the result.
 */
typedef struct {
	jxcolumn_t oldc;
	jxcolumn_t newc;
	jxdiffstyle_t diffstyle;
	int	valuewidth;
	jx_t	*result;
} jxdiff_t;

/* Comparison function for sorting an object's keys */
static int cmp_keys(const void *p1, const void *p2)
{
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
		sum += *json->text;
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

/* Initialize data that will help us find differences */
static jxcolumn_t j_to_c(jx_t *json)
{
	jx_t	*scan;
	jxcolumn_t col;
	int	i;

	/* Initialize the easy fields */
	col.json = json;
	col.len = jx_length(json);
	col.cur = 0;

	/* Build the hash array.  If json is an object then we also add
	 * pointers to each member, and then sort the elements by member key;
	 * this lets us easily work around differences in member order.
	 */
	col.array = calloc(col.len, sizeof *col.array);
	if (json->type == JX_OBJECT) {
		for (i = 0, scan = json->first; scan; i++, scan = scan->next) {
			col.array[i].hash = jx_hash(scan, 0);
			col.array[i].key = scan;
		}
		qsort(col.array, col.len, sizeof *col.array, cmp_keys);
	} else {
		for (i = 0, scan = jx_first(json); scan; i++, scan = jx_next(scan)) {
			col.array[i].hash = jx_hash(scan, 0);
		}
	}

	/* Return the whole struct */
	return col;
}

/* Return the index as a jx_t.  When comparing objects, this will be a string
 * containing the member's key.  For arrays, this will be a number.
 */
static jx_t *diff_index(jxcolumn_t *col, int offset)
{
	if (col->array[col->cur + offset].key)
		return jx_string(col->array[col->cur + offset].key->text, -1);
	return jx_from_int(col->cur + offset);
}

/* Return a value trying to limit the width.  For arrays/objects, convert to
 * string
 */
static jx_t *diff_value(jxcolumn_t *col, int offset, int width)
{
	char	*text, *scan, *mustfree = NULL;
	wchar_t	wc;
	int	in, charwidth, scanwidth;
	jx_t	*value, *str;

	/* Get the value.  If we're diffing objects, use the key's value not
	 * the key.
	 */
	value = col->array[col->cur + offset].key;
	if (value)
		value = value->first;
	else
		value = jx_by_index(col->json, col->cur + offset);

	/* For simple numbers/symbols, just return it.  Otherwise get text */
	if (value->type == JX_STRING)
		text = value->text;
	else if (value->type == JX_ARRAY || value->type == JX_OBJECT)
		text = mustfree = jx_serialize(value, NULL);
	else {
		str = jx_copy(value);
		jx_break(value);
		return str;
	}

	/* See how many bytes of the requested value we can fit in the width */
	for (scan = text, scanwidth = 0; *scan; scan += in) {
		/* Stop at the first control character */
		if (*scan > 0 && *scan < ' ')
			break;

		/* Convert the next UTF-8 character to a wchar_t */
		in = mbtowc(&wc, scan, MB_CUR_MAX);
		if (in <= 0) /* invalid UTF-8 coding */
			break;

		/* Get the width of the character.  Does it fit? */
		charwidth = wcwidth(wc);
		if (scanwidth + charwidth > width)
			break;
		scanwidth += charwidth;
	}

	/* Allocate a string to hold clipped text */
	str = jx_string(text, scan - text);

	/* Clean up and return the string */
	if (mustfree)
		free(mustfree);
	jx_break(value);
	return str;
}


/* Add an entry (or entries, depending on dp->diffstyle) to the result, and then
 * advance dp->oldc.cur and dp->newc.cur to the next match.
 */
static void add_diff(jxdiff_t *dp, int oldspan, int newspan)
{
	jx_t	*entry;
	char	*edit;
	int	i;

	/* "span" style is handled differently. */
	if (dp->diffstyle & JX_DIFF_SPAN) {
		/* Start with an empty row */
		entry = jx_object();

		/* Only works on arrays, so we know we "old" and "new" are
		 * index numbers.
		 */
		jx_append(entry, jx_key("old", jx_from_int(dp->oldc.cur)));
		jx_append(entry, jx_key("oldspan", jx_from_int(oldspan)));
		if (dp->diffstyle & JX_DIFF_EDIT) {
			if (oldspan == 0)
				edit = "insert";
			else if (newspan == 0)
				edit = "delete";
			else
				edit = "change";
			jx_append(entry, jx_key("edit", jx_string(edit, -1)));
		}
		jx_append(entry, jx_key("new", jx_from_int(dp->newc.cur)));
		jx_append(entry, jx_key("newspan", jx_from_int(newspan)));

		/* Add the entry to the result, and adjust "cur" */
		jx_append(dp->result, entry);
		dp->oldc.cur += oldspan;
		dp->newc.cur += newspan;
		return;
	}

	/* "beside" handles differences in old and new as pairs */
	if (dp->diffstyle & JX_DIFF_BESIDE) {
		/* Add a "change" entry for each pair of overlapping lines */
		for (i = 0; i < oldspan && i < newspan; i++) {
			entry = jx_object();
			jx_append(entry, jx_key("old", diff_index(&dp->oldc, i)));
			if (dp->diffstyle & JX_DIFF_VALUE)
				jx_append(entry, jx_key("oldvalue", diff_value(&dp->oldc, i, dp->valuewidth)));
			if (dp->diffstyle & JX_DIFF_EDIT)
				jx_append(entry, jx_key("edit", jx_string("change", -1)));
			jx_append(entry, jx_key("new", diff_index(&dp->newc, i)));
			if (dp->diffstyle & JX_DIFF_VALUE)
				jx_append(entry, jx_key("newvalue", diff_value(&dp->newc, i, dp->valuewidth)));
			jx_append(dp->result, entry);
		}

		/* NOTE: At this point, we've exhausted either the oldspan or
		 * the newspan.  Either way, we'll only execute one of the
		 * following loops.
		 */

		/* Add a "delete" entry for each line only in "old" */
		for (; i < oldspan; i++) {
			entry = jx_object();
			jx_append(entry, jx_key("old", diff_index(&dp->oldc, i)));
			if (dp->diffstyle & JX_DIFF_VALUE)
				jx_append(entry, jx_key("oldvalue", diff_value(&dp->oldc, i, dp->valuewidth)));
			if (dp->diffstyle & JX_DIFF_EDIT)
				jx_append(entry, jx_key("edit", jx_string("delete", -1)));
			jx_append(entry, jx_key("new", jx_null()));
			if (dp->diffstyle & JX_DIFF_VALUE)
				jx_append(entry, jx_key("newvalue", jx_null()));
			jx_append(dp->result, entry);
		}

		/* Add an "insert" entry for each line only in "new" */
		for (; i < newspan; i++) {
			entry = jx_object();
			jx_append(entry, jx_key("old", jx_null()));
			if (dp->diffstyle & JX_DIFF_VALUE)
				jx_append(entry, jx_key("oldvalue", jx_null()));
			if (dp->diffstyle & JX_DIFF_EDIT)
				jx_append(entry, jx_key("edit", jx_string("insert", -1)));
			jx_append(entry, jx_key("new", diff_index(&dp->newc, i)));
			if (dp->diffstyle & JX_DIFF_VALUE)
				jx_append(entry, jx_key("newvalue", diff_value(&dp->newc, i, dp->valuewidth)));
			jx_append(dp->result, entry);
		}

		/* Adjust "cur" in each column */
		dp->oldc.cur += oldspan;
		dp->newc.cur += newspan;
		return;
	}

	/* Without "span" or "beside" we generate separate entries for old
	 * and new.
	 */
	for (i = 0; i < oldspan; i++) {
		entry = jx_object();
		jx_append(entry, jx_key("index", diff_index(&dp->oldc, i)));
		jx_append(entry, jx_key("edit", jx_string("delete", -1)));
		if (dp->diffstyle & JX_DIFF_VALUE)
			jx_append(entry, jx_key("value", diff_value(&dp->newc, i, dp->valuewidth)));
		jx_append(dp->result, entry);
	}
	for (i = 0; i < newspan; i++) {
		entry = jx_object();
		jx_append(entry, jx_key("index", diff_index(&dp->newc, i)));
		jx_append(entry, jx_key("edit", jx_string("delete", -1)));
		if (dp->diffstyle & JX_DIFF_VALUE)
			jx_append(entry, jx_key("value", diff_value(&dp->newc, i, dp->valuewidth)));
		jx_append(dp->result, entry);
	}

	/* Adjust "cur" in each column */
	dp->oldc.cur += oldspan;
	dp->newc.cur += newspan;
}

/* Search for differences between two arrays */
jx_t *jx_diff(jx_t *oldj, jx_t *newj, jxdiffstyle_t diffstyle)
{
	jxdiff_t d;
	int	reach, check;
	jxhash_t *oldbase, *newbase;

	/* Check arguments for compatibility */
	if (oldj->type == JX_ARRAY && newj->type == JX_ARRAY) {
		/* Good! */
	} else if (oldj->type == JX_OBJECT && newj->type == JX_OBJECT) {
		/* Also good, but can't do "span" or "context" styles */
		diffstyle &= ~(JX_DIFF_SPAN | JX_DIFF_CONTEXT);
	} else {
		return jx_error_null(NULL, "diffargs:Incompatible args for diff");
	}

	/* Initialize */
	d.oldc = j_to_c(oldj);
	d.newc = j_to_c(newj);
	d.diffstyle = diffstyle;
	d.valuewidth = (diffstyle & JX_DIFF_BESIDE) ? 32 : 60;
	d.result = jx_array();

	/* Scan until we hit the end of either column */
	while (d.oldc.cur < d.oldc.len && d.newc.cur < d.newc.len) {

		/* Skip matching lines. */
		if (d.oldc.array[d.oldc.cur].hash == d.newc.array[d.newc.cur].hash) {
			d.oldc.cur++;
			d.newc.cur++;
			continue;
		}

		/* Look for the nearest pair of matching lines */
		oldbase = &d.oldc.array[d.oldc.cur];
		newbase = &d.newc.array[d.newc.cur];
		for (reach = 1; d.oldc.cur + reach < d.oldc.len || d.newc.cur + reach < d.newc.len; reach++) {
			for (check = 0; check <= reach; check++)  {
				if (d.oldc.cur + check < d.oldc.len
				 && d.newc.cur + reach < d.newc.len
				 && oldbase[check].hash == newbase[reach].hash){
					/* Match! */
					add_diff(&d, check, reach);
					goto ContinueWhile;
				}
				if (d.oldc.cur + reach < d.oldc.len
				 && d.newc.cur + check < d.newc.len
				 && oldbase[reach].hash == newbase[check].hash){
					add_diff(&d, reach, check);
					goto ContinueWhile;
				}
			}
		}

		/* If we get here, then we hit the end of both of the arrays
		 * without finding a match.  Count it as one big mismatch.
		 */
		add_diff(&d, d.oldc.len - d.oldc.cur, d.newc.len - d.newc.cur);
ContinueWhile:
		;
	}

	/* It is possible that we still have items in one of the arrays.
	 * If so, these should count as a mismatch.  (The similar test inside
	 * the above loop catches cases where there's a difference that extends
	 * to the ends of the arrays.  This test catches cases where the last
	 * items matched but one input has extra items after that.)
	 */
	if (d.oldc.cur < d.oldc.len || d.newc.cur < d.newc.len)
		add_diff(&d, d.oldc.len - d.oldc.cur, d.newc.len - d.newc.cur);

	/* Clean up */
	free(d.oldc.array);
	free(d.newc.array);

	/* Return the result */
	return d.result;
}
