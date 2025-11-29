#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jx.h>


/* Compare two jx_t values for equality.  Return 1 if equal, 0 if different.
 * This compares type as well as value.  It can even compare arrays (same types
 * and values in the same order) and objects (same member names and values
 * in any order).
 */
int jx_equal(jx_t *j1, jx_t *j2)
{
        jx_t  *tmp;

        /* Trivial case */
        if (j1 == j2)
                return 1;

        /* Different types don't match */
        if (j1->type !=  j2->type)
                return 0;

        /* Compare types as appropriate */
        switch (j1->type) {
          case JX_BOOLEAN:
          case JX_STRING:
                /* Compare their literal text, case-sensitively. */
                return !strcmp(j1->text, j2->text);

	  case JX_NULL:
		return 1;

          case JX_NUMBER:
		/* Numbers may be binary or text. */
		if (j1->text[0] == '\0' && j1->text[1] == 'i'
		 && j2->text[0] == '\0' && j2->text[1] == 'i')
			return JX_INT(j1) == JX_INT(j2);
		if (j1->text[0] == '\0' && j1->text[1] == 'd'
		 && j2->text[0] == '\0' && j2->text[1] == 'd')
			return JX_DOUBLE(j1) == JX_DOUBLE(j2);
		return jx_double(j1) == jx_double(j2);

          case JX_ARRAY:
                /* Compare length, and values of elements. */
                if (jx_length(j1) != jx_length(j2))
                        return 0;
                for (j1 = jx_first(j1), j2 = jx_first(j2); j1 && j2; j1 = jx_next(j1), j2 = jx_next(j2)) {
                        if (!jx_equal(j1, j2)) {
				jx_break(j1);
				jx_break(j2);
                                return 0;
			}
                }
                return 1;

          case JX_OBJECT:
                /* Compare length, and values/names of members.  It's okay if
                 * the members are listed in a different order; we find them
                 * by name.
                 */
                if (jx_length(j1) != jx_length(j2))
                        return 0;
                for (j1 = j1->first; j1; j1 = j1->next) { /* object */
                        tmp = jx_by_key(j2, j1->text);
                        if (!tmp || !jx_equal(j1->first, tmp))
                                return 0;
                }
                return 1;

          default:
                /* shouldn't happen */
                return 0;
        }
}
