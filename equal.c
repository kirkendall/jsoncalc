#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "json.h"


/* Compare two json_t values for equality.  Return 1 if equal, 0 if different.
 * This compares type as well as value.  It can even compare arrays (same types
 * and values in the same order) and objects (same member names and values
 * in any order).
 */
int json_equal(json_t *j1, json_t *j2)
{
        json_t  *tmp;

        /* Trivial case */
        if (j1 == j2)
                return 1;

        /* Different types don't match */
        if (j1->type !=  j2->type)
                return 0;

        /* Compare types as appropriate */
        switch (j1->type) {
          case JSON_SYMBOL:
          case JSON_STRING:
          case JSON_NUMBER:
                /* Compare their literal text */
                return !strcmp(j1->text, j2->text);

          case JSON_ARRAY:
                /* Compare length, and values of elements */
                if (json_length(j1) != json_length(j2))
                        return 0;
                for (j1 = j1->first, j2 = j2->first; j1 && j2; j1 = j1->next, j2 = j2->next) {
                        if (!json_equal(j1, j2))
                                return 0;
                }
                return 1;

          case JSON_OBJECT:
                /* Compare length, and values/names of members.  It's okay if
                 * the members are listed in a different order; we find them
                 * by name.
                 */
                if (json_length(j1) != json_length(j2))
                        return 0;
                for (j1 = j1->first; j1; j1 = j1->next) {
                        tmp = json_by_key(j2, j1->text);
                        if (!tmp || !json_equal(j1->first, tmp))
                                return 0;
                }
                return 1;

          default:
                /* shouldn't happen */
                return 0;
        }
}
