#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jsoncalc.h>

/* Test whether a JSON value is true.  Everything is true except for the
 * symbols "false" and "null", the number 0, an empty string, or an empty
 * array/object.
 */
int json_is_true(json_t *json)
{
        /* NULL pointer is false */
        if (!json)
		return 0;

        /* Otherwise it depents on type and value */
        switch (json->type) {
          case JSON_BOOL:
                return json->text[0] == 't';
	  case JSON_NULL:
		return 0;
          case JSON_STRING:
                return json->text[0] != '\0';
          case JSON_NUMBER:
                if (json->text[0] == '0')
			return 0; /* 0 in string form */
		if (json->text[0] != '\0')
			return 1; /* non-0 in string form */
                if (json->text[1] == 'i')
			return JSON_INT(json) != 0; /* binary integer */
                return JSON_DOUBLE(json) != 0.0; /* binary double */
          case JSON_ARRAY:
          case JSON_OBJECT:
                return json->first != NULL;
          default: /* JSON_KEY? Shouldn't happen */
                return 0;
        }
}

/* Test whether a JSON value is NULL.  This could be either because the
 * pointer is a NULL pointer (which generally means the value is absent
 * from an object) or the symbol "null".
 */
int json_is_null(json_t *json)
{
	return (!json || json->type == JSON_NULL);
}

/* Test whether a JSON value is a NULL that represents an error. */
int json_is_error(json_t *json)
{
	return (json && json->type == JSON_NULL && json->text[0]);
}

/* Test whether a JSON value is an array of objects */
int json_is_table(json_t *json)
{
	int	anydata;

        /* Must be an array */
        if (!json || json->type != JSON_ARRAY)
                return 0;

        /* Every element must be an object.  Must contain some data */
        anydata = 0;
        for (json = json->first; json; json = json->next) {
                if (json->type != JSON_OBJECT)
                        return 0;
		if (json->first)
			anydata = 1;
	}
	if (!anydata)
		return 0;

        /* Looks good.  Might be an empty array, but that's okay */
        return 1;
}


/* This is a recursive function to help json_is_short().  It returns an
 * estimate of the length, but stops counting once the "oneline" threshold
 * has been crossed.  This is only approximate!
 */
static size_t shorthelper(json_t *json, size_t oneline)
{
        size_t size = 0;

        while (json && size < oneline) {
                /* Text and punctuation */
                switch (json->type) {
                  case JSON_STRING:
                        size += strlen(json->text) + 2; /* ignoring escapes */
                        break;
                  case JSON_NUMBER:
			if (*json->text)
				size += strlen(json->text);
			else if (json->text[1] == 'i') {
				int i = JSON_INT(json);
				if (i < 0) {
					size++; /* for "-" */
					i = -i;
				}
				if (i < 10)
					size += 1;
				else if (i < 100)
					size += 2;
				else if (i < 1000)
					size += 3;
				else
					size += 10;
			}
			else
				size += 10;
			break;
                  case JSON_NULL:
			size += 4;
			break;
                  case JSON_BOOL:
                        size += strlen(json->text);
                        break;
                  case JSON_KEY:
                        size += strlen(json->text) + 3;

			/* Handle "first" recursively */
			size += shorthelper(json->first, oneline);
			if (size >= oneline)
				return size;
                        break;
                  default:
                        size += 2; /* for "[]" or "{}" */

			/* Handle "first" recursively */
			size += shorthelper(json->first, oneline);
			if (size >= oneline)
				return size;
                }

                /* Handle "next" iteratively */
                size++; /* for "," */
                json = json->next;
        }
        return size;
}

/* Test whether the serialized version of an expression is short.  This is
 * quick -- it stops counting once the non-short threshold has been crossed.
 * Also, it uses approximations so the "oneline" parameter is not precise.
 */
int json_is_short(json_t *json, size_t oneline)
{
        return shorthelper(json, oneline) < oneline;
}

/* Return 1 iff json looks like an ISO date string "YYYY-MM-DD" */
int json_is_date(json_t *json)
{
	if (!json || json->type != JSON_STRING || !json_str_date(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO time string "hh:mm:ss" */
int json_is_time(json_t *json)
{
	if (!json || json->type != JSON_STRING || !json_str_time(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO datetime string "YYYY-MM-DDThh:mm:ss" */
int json_is_datetime(json_t *json)
{
	if (!json || json->type != JSON_STRING || !json_str_datetime(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO period string "PnYnMnWnDTnHnMnS" */
int json_is_period(json_t *json)
{
	if (!json || json->type != JSON_STRING || !json_str_period(json->text))
		return 0;
	return 1;
}
