#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "json.h"

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
          case JSON_SYMBOL:
                return json->text[0] == 't';
          case JSON_STRING:
                return json->text[0] != '\0';
          case JSON_NUMBER:
                return json->text[0] != '0' || json->text[1] != '\0';
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
	return (!json || (json->type == JSON_SYMBOL && *json->text == 'n'));
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

        while (json) {
                /* Text and punctuation */
                switch (json->type) {
                  case JSON_STRING:
                        size += strlen(json->text) + 2; /* ignoring escapes */
                        break;
                  case JSON_NUMBER:
                  case JSON_SYMBOL:
                        size += strlen(json->text);
                        break;
                  case JSON_KEY:
                        size += strlen(json->text) + 3;
                        break;
                  default:
                        size += 2; /* for "[]" or "{}" */
                }

                /* Handle "first" recursively */
                size += shorthelper(json->first, oneline);
                if (size >= oneline)
                        return size;

                /* Handle "next" recursively */
                size++; /* for "," */
                json = json->next;
        }
        return size;
}

/* Test whether the serialized version of an expression is short.  This is
 * quick -- it stops counting once the non-short threshold has been crossed.
 */
int json_is_short(json_t *json, size_t oneline)
{
        return shorthelper(json, oneline) < oneline;
}
