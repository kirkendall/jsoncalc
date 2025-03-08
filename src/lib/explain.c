#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include "json.h"
#include "calc.h"



/* Return a string describing the data type of a json_t.  Possible return
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
char *json_typeof(json_t *json, int extended)
{
	/* Defend against NULL */
	if (!json)
		return "null";

	/* First clue is the "type" field */
	switch (json->type) {
	  case JSON_NUMBER:
		return "number";
	  case JSON_OBJECT:
		if (!json->first && extended)
			return "object*";
		return "object";
	  case JSON_BOOL:
		return "boolean";
	  case JSON_NULL:
		return "null";
	  case JSON_STRING:
		/* Strings may be dates, times, or datetimes. Or just strings */
		if (extended) {
			if (!*json->text)
				return "string*";
			if (json_is_date(json))
				return "date";
			if (json_is_time(json))
				return "time";
			if (json_is_datetime(json))
				return "datetime";
			if (json_is_period(json))
				return "period";
		}
		return "string";
	  case JSON_ARRAY:
		/* If it's a non-empty array of objects, call it a table.
		 * Otherwise, it's an array.
		 */
		if (extended) {
			if (!json->first)
				return "array*";
			if (json_is_table(json))
				return "table";
		}
		return "array";
	  default:
		/* Shouldn't happen */
		return "null";
	}
}

/* Combine oldtype and newtype.  Try to keep the result as specific as
 * possible.  If total chaos, just return "mixed".
 */
char *json_mix_types(char *oldtype, char *newtype)
{
	/* If typenames are the same, or oldtype is "mixed", it's easy */
	if (!strcmp(oldtype, newtype) || !strcmp(oldtype, "mixed"))
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
	return "mixed";
}

/* Collect column info from a single row and merge it into aggregated info. If
 * the aggregated info is NULL, allocate it.  Depth is 0 normally, or higher
 * values to allow embedded objects and tables (arrays of objects) to also
 * be explained -- 1 allows a single layer, 2 for 2 layers deep, etc.  -1
 * will allow any depth.
 *
 * Returns the updated aggregated data, as an array of objects describing each
 * column.  When the aggregated data is no longer needed, you must free it
 * via the usual json_free() function.
 */
json_t *json_explain(json_t *columns, json_t *row, int depth)
{
	json_t *col, *stats, *t, *t2;
	char	*newtype, *oldtype;
	int	newwidth, oldwidth;
	int	firstrow;
	char	number[40];

	/* If row isn't an object, then we can't do much with it */
	if (!row || row->type != JSON_OBJECT)
		return columns;

	/* Allocate an array to store the columns, if we don't have one yet */
	firstrow = 0;
	if (!columns) {
		columns = json_array();
		firstrow = 1;
	}

	/* For each column of the row ... */
	for (col = row->first; col; col = col->next) {
		assert(col->type == JSON_KEY);

		/* Derive the type by examining the key's value */
		newtype = json_typeof(col->first, 1);

		/* Locate the columns entry for this line, if any */
		for (stats = columns->first; stats; stats = stats->next) {
			if ((t = json_by_key(stats, "key")) != NULL
			 && t->type == JSON_STRING
			 && !strcmp(t->text, col->text))
				break;
		}

		/* Are we updating existing stats for this column? */
		if (stats) {
			/* Yes, we have existing stats.  Update it */

			/* If newtype is "null" then the element is nullable */
			if (!strcmp(newtype, "null"))
				json_append(stats, json_key("nullable", json_bool(1)));

			/* Mixing types is  bit tricky */
			oldtype = json_text_by_key(stats, "type");
			newtype = json_mix_types(oldtype, newtype);
			if (newtype)
				json_append(stats, json_key("type", json_string(newtype, -1)));
			newtype = oldtype;

			/* Get the new width.  The biggest complication here
			 * is that sometimes numbers are binary.
			 */
			if (col->first->type == JSON_NUMBER && !col->first->text[0]) {
				if (col->first->text[1] == 'i')
					snprintf(number, sizeof number, "%d", JSON_INT(col->first));
				else
					snprintf(number, sizeof number, "%.*g", json_format_default.digits, JSON_DOUBLE(col->first));
				newwidth = strlen(number);
			} else  {
				newwidth = json_mbs_width(col->first->text);
			}

			/* Width can only increase */
			oldwidth = json_int(json_by_key(stats, "width"));
			if (newwidth > oldwidth)
				json_append(stats, json_key("width", json_from_int(newwidth)));

		} else {
			/* No, this is a new column.  Add it. */
			stats = json_object();
			json_append(stats, json_key("key", json_string(col->text, -1)));
			json_append(stats, json_key("type", json_string(newtype, -1)));
			if (col->first->type == JSON_NUMBER && !col->first->text[0]) {
				if (col->first->text[1] == 'i')
					snprintf(number, sizeof number, "%d", JSON_INT(col->first));
				else
					snprintf(number, sizeof number, "%.*g", json_format_default.digits, JSON_DOUBLE(col->first));
				newwidth = strlen(number);
			} else {
				newwidth = json_mbs_width(col->first->text);
			}
			json_append(stats, json_key("width", json_from_int(newwidth)));
			json_append(stats, json_key("nullable", json_bool(!strcmp(newtype, "null") || !firstrow)));
			json_append(columns, stats);
			oldtype = newtype;
		}

		/* Do we want to recurse for opjects/tables? */
		if (depth != 0) {
			if (!strcmp(newtype, "object") && col->first->type == JSON_OBJECT) {
				t = json_by_key(stats, "explain");
				t2 = json_explain(t, col->first, depth - 1);
				if (!t)
					json_append(stats, json_key("explain", t2));
			} else if (!strcmp(newtype, "table") && col->first->type == JSON_ARRAY) {
				t = json_by_key(stats, "explain");
				for (t2 = col->first->first; t2; t2 = t2->next) {
					t = json_explain(t, t2, depth - 1);
				}
				if (json_by_key(stats, "explain") != t)
					json_append(stats, json_key("explain", t));
			}
		}

	}

	/* If any known columns are missing from this row, assume the column
	 * is nullable.
	 */
	for (stats = columns->first; stats; stats = stats->next) {
		if (json_by_key(row, json_text_by_key(stats, "key")) == NULL)
			json_append(stats, json_key("nullable", json_bool(1)));
	}

	/* Return the array of stats */
	return columns;
}
