#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <jx.h>



/* Return a string describing the data type of a jx_t.  Possible return
 * values are:
 *   "boolean"	The symbols true and false.
 *   "null"	A NULL pointer, or the symbol null, or unexpected/error values
 *   "object"	Any object
 *   "number"	Any number
 *   "string"	Any other string
 *   "array"	Any other array
 * If "extended" is true, then it can refine the response as follows:
 *   "array*"	Empty array
 *   "table"	A non-empty array of objects
 *   "object*"  Empty object
 *   "date"     A string that looks like an ISO-8601 date
 *   "time"     A string that looks like an ISO-8601 time
 *   "datetime"	A string that looks like an ISO-8601 datetime
 *   "period"	A string that looks like an ISO-8601 period
 */
char *jx_typeof(jx_t *json, int extended)
{
	/* Defend against NULL */
	if (!json)
		return "null";

	/* First clue is the "type" field */
	switch (json->type) {
	  case JX_NUMBER:
		return "number";
	  case JX_OBJECT:
		if (!json->first && extended)
			return "object*";
		return "object";
	  case JX_BOOLEAN:
		return "boolean";
	  case JX_NULL:
		return "null";
	  case JX_STRING:
		/* Strings may be dates, times, or datetimes. Or just strings */
		if (extended) {
			if (!*json->text)
				return "string*";
			if (jx_is_date(json))
				return "date";
			if (jx_is_time(json))
				return "time";
			if (jx_is_datetime(json))
				return "datetime";
			if (jx_is_period(json))
				return "period";
		}
		return "string";
	  case JX_ARRAY:
		/* If it's a non-empty array of objects, call it a table.
		 * Otherwise, it's an array.
		 */
		if (extended) {
			if (!json->first)
				return "array*";
			if (jx_is_table(json))
				return "table";
		}
		return "array";
	  default:
		/* Shouldn't happen */
		return "null";
	}
}

/* Combine oldtype and newtype.  Try to keep the result as specific as
 * possible.  If total chaos, just return "any".
 */
char *jx_mix_types(char *oldtype, char *newtype)
{
	/* If typenames are the same, or oldtype is "any", it's easy */
	if (!strcmp(oldtype, newtype) || !strcmp(oldtype, "any"))
		return NULL;

	/* If oldtype is "null" then we have more info now! */
	if (!strcmp(oldtype, "null"))
		return newtype;

	/* If newtype is "null" then that doesn't give us any info */
	if (!strcmp(newtype, "null"))
		return oldtype;

	/* Bad XML conversions can't distinguish between empty strings,
	 * empty arrays, or empty objects.  Don't let an empty value mess up
	 * what we thought we knew about the type.
	 */
	if (strchr(newtype, '*'))
		return oldtype;
	if (strchr(oldtype, '*'))
		return newtype;

	/* Mixing "object*" and "object" is "object" */
	if (!strncmp(oldtype, "object", 6) && !strncmp(newtype, "object", 6))
		return "object";

	/* Mixing "table" and "array" is an "array" */
	if ((!strcmp(newtype, "table") || !strncmp(newtype, "array", 5))
	 && (!strcmp(oldtype, "table") || !strncmp(oldtype, "array", 5)))
		return "array";

	/* "date", "time", and "datetime" are all variations of string.
	 * If they don't match then "string" is the safest classification.
	 */
	if ((!strncmp(newtype, "date", 4) || !strcmp(newtype, "time") || !strcmp(newtype, "string"))
	 && (!strncmp(oldtype, "date", 4) || !strcmp(oldtype, "time") || !strcmp(oldtype, "string")))
		return "string";

	/* Chaos */
	return "any";
}

/* Collect column info from a single row and merge it into aggregated info. If
 * the aggregated info is NULL, allocate it.  Depth is 0 normally, or higher
 * values to allow embedded objects and tables (arrays of objects) to also
 * be explained -- 1 allows a single layer, 2 for 2 layers deep, etc.  -1
 * will allow any depth.
 *
 * Returns the updated aggregated data, as an array of objects describing each
 * column.  When the aggregated data is no longer needed, you must free it
 * via the usual jx_free() function.
 */
jx_t *jx_explain(jx_t *columns, jx_t *row, int depth)
{
	jx_t *col, *stats, *t, *t2;
	char	*newtype, *oldtype;
	int	newwidth, oldwidth;
	int	firstrow;
	char	number[40];

	/* If row isn't an object, then we can't do much with it */
	if (!row || row->type != JX_OBJECT)
		return columns;

	/* Allocate an array to store the columns, if we don't have one yet */
	firstrow = 0;
	if (!columns) {
		columns = jx_array();
		firstrow = 1;
	}

	/* For each column of the row ... */
	for (col = row->first; col; col = col->next) { /* object */
		assert(col->type == JX_KEY);

		/* Derive the type by examining the key's value */
		newtype = jx_typeof(col->first, 1);

		/* Locate the columns entry for this line, if any */
		for (stats = columns->first; stats; stats = stats->next) { /* undeferred */
			if ((t = jx_by_key(stats, "key")) != NULL
			 && t->type == JX_STRING
			 && !strcmp(t->text, col->text))
				break;
		}

		/* Are we updating existing stats for this column? */
		if (stats) {
			/* Yes, we have existing stats.  Update it */

			/* If newtype is "null" then the element is nullable */
			if (!strcmp(newtype, "null"))
				jx_append(stats, jx_key("nullable", jx_boolean(1)));

			/* Mixing types is  bit tricky */
			oldtype = jx_text_by_key(stats, "type");
			newtype = jx_mix_types(oldtype, newtype);
			if (newtype)
				jx_append(stats, jx_key("type", jx_string(newtype, -1)));
			newtype = oldtype;

			/* Get the new width.  The biggest complication here
			 * is that sometimes numbers are binary.
			 */
			if (col->first->type == JX_NUMBER && !col->first->text[0]) {
				if (col->first->text[1] == 'i')
					snprintf(number, sizeof number, "%d", JX_INT(col->first));
				else
					snprintf(number, sizeof number, "%.*g", jx_format_default.digits, JX_DOUBLE(col->first));
				newwidth = strlen(number);
			} else  {
				newwidth = jx_mbs_width(col->first->text);
			}

			/* Width can only increase */
			oldwidth = jx_int(jx_by_key(stats, "width"));
			if (newwidth > oldwidth)
				jx_append(stats, jx_key("width", jx_from_int(newwidth)));

		} else {
			/* No, this is a new column.  Add it. */
			stats = jx_object();
			jx_append(stats, jx_key("key", jx_string(col->text, -1)));
			jx_append(stats, jx_key("type", jx_string(newtype, -1)));
			if (col->first->type == JX_NUMBER && !col->first->text[0]) {
				if (col->first->text[1] == 'i')
					snprintf(number, sizeof number, "%d", JX_INT(col->first));
				else
					snprintf(number, sizeof number, "%.*g", jx_format_default.digits, JX_DOUBLE(col->first));
				newwidth = strlen(number);
			} else {
				newwidth = jx_mbs_width(col->first->text);
			}
			jx_append(stats, jx_key("width", jx_from_int(newwidth)));
			jx_append(stats, jx_key("nullable", jx_boolean(!strcmp(newtype, "null") || !firstrow)));
			jx_append(columns, stats);
			oldtype = newtype;
		}

		/* Do we want to recurse for opjects/tables? */
		if (depth != 0) {
			if (!strcmp(newtype, "object") && col->first->type == JX_OBJECT) {
				t = jx_by_key(stats, "explain");
				t2 = jx_explain(t, col->first, depth - 1);
				if (!t)
					jx_append(stats, jx_key("explain", t2));
			} else if (!strcmp(newtype, "table") && col->first->type == JX_ARRAY) {
				t = jx_by_key(stats, "explain");
				for (t2 = col->first->first; t2; t2 = t2->next) { /* undeferred */
					t = jx_explain(t, t2, depth - 1);
				}
				if (jx_by_key(stats, "explain") != t)
					jx_append(stats, jx_key("explain", t));
			}
		}

	}

	/* If any known columns are missing from this row, assume the column
	 * is nullable.
	 */
	for (stats = columns->first; stats; stats = stats->next) { /* undeferred */
		if (jx_by_key(row, jx_text_by_key(stats, "key")) == NULL)
			jx_append(stats, jx_key("nullable", jx_boolean(1)));
	}

	/* Return the array of stats */
	return columns;
}
