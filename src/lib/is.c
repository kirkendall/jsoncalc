#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jx.h>

/* Test whether a JSON value is true.  Everything is true except for the
 * symbols "false" and "null", the number 0, an empty string, or an empty
 * array/object.
 */
int jx_is_true(jx_t *json)
{
        /* NULL pointer is false */
        if (!json)
		return 0;

        /* Otherwise it depends on type and value */
        switch (json->type) {
          case JX_BOOLEAN:
                return json->text[0] == 't';
	  case JX_NULL:
		return 0;
          case JX_STRING:
                return json->text[0] != '\0';
          case JX_NUMBER:
                if (json->text[0] == '0')
			return 0; /* 0 in string form */
		if (json->text[0] != '\0')
			return 1; /* non-0 in string form */
                if (json->text[1] == 'i')
			return JX_INT(json) != 0; /* binary integer */
                return JX_DOUBLE(json) != 0.0; /* binary double */
          case JX_ARRAY:
          case JX_OBJECT:
                return json->first != NULL;
          default: /* JX_KEY? Shouldn't happen */
                return 0;
        }
}

/* Test whether a JSON value is NULL.  This could be either because the
 * pointer is a NULL pointer (which generally means the value is absent
 * from an object) or the symbol "null".
 */
int jx_is_null(jx_t *json)
{
	return (!json || json->type == JX_NULL);
}

/* Test whether a JSON value is a NULL that represents an error. */
int jx_is_error(jx_t *json)
{
	return (json && json->type == JX_NULL && json->text[0]);
}

/* Test whether a JSON value is an array of objects */
int jx_is_table(jx_t *json)
{
	int	anydata;
	jx_t	*elem;

        /* Must be an array */
        if (!json || json->type != JX_ARRAY)
                return 0;

	/* If we already have an answer in ->text[1], use it */
	if (json->text[1] == 't')
		return 1;
	else if (json->text[1] == 'n')
		return 0;

        /* Every element must be a non-empty object. */
        anydata = 0;
        for (elem = jx_first(json); elem; elem = jx_next(elem)) {
                if (elem->type != JX_OBJECT) {
			jx_break(elem);
			json->text[1] = 'n';
                        return 0;
		}
		if (elem->first)
			anydata = 1;
	}
	if (!anydata) {
		json->text[1] = 'n';
		return 0;
	}

        /* Looks good. */
        json->text[1] = 't';
        return 1;
}


/* This is a recursive function to help jx_is_short().  It returns an
 * estimate of the length, but stops counting once the "oneline" threshold
 * has been crossed.  This is only approximate!
 */
static size_t shorthelper(jx_t *json, size_t oneline)
{
        size_t size = 0;

        while (json && size < oneline) {
		/* Assume deferred arrays are long */
		if (jx_is_deferred_array(json))
			return oneline;

                /* Text and punctuation */
                switch (json->type) {
                  case JX_STRING:
                        size += strlen(json->text) + 2; /* ignoring escapes */
                        break;
                  case JX_NUMBER:
			if (*json->text)
				size += strlen(json->text);
			else if (json->text[1] == 'i') {
				int i = JX_INT(json);
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
                  case JX_NULL:
			size += 4;
			break;
                  case JX_BOOLEAN:
                        size += strlen(json->text);
                        break;
                  case JX_KEY:
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
                json = json->next; /* undeferred */
        }
        return size;
}

/* Test whether the serialized version of an expression is short.  This is
 * quick -- it stops counting once the non-short threshold has been crossed.
 * Also, it uses approximations so the "oneline" parameter is not precise.
 */
int jx_is_short(jx_t *json, size_t oneline)
{
        return shorthelper(json, oneline) < oneline;
}

/* Return 1 iff json looks like an ISO date string "YYYY-MM-DD" */
int jx_is_date(jx_t *json)
{
	if (!json || json->type != JX_STRING || !jx_str_date(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO time string "hh:mm:ss" */
int jx_is_time(jx_t *json)
{
	if (!json || json->type != JX_STRING || !jx_str_time(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO datetime string "YYYY-MM-DDThh:mm:ss" */
int jx_is_datetime(jx_t *json)
{
	if (!json || json->type != JX_STRING || !jx_str_datetime(json->text))
		return 0;
	return 1;
}

/* Return 1 iff json looks like an ISO period string "PnYnMnWnDTnHnMnS" */
int jx_is_period(jx_t *json)
{
	if (!json || json->type != JX_STRING || !jx_str_period(json->text))
		return 0;
	return 1;
}
