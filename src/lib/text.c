#include <stdlib.h>
#include <stdio.h>
#include <jx.h>

static char *defaultvalue;

/* Set the default value for jx_text, when no data is found.  Returns the
 * old default.
 */
char *jx_default_text(char *newdefault)
{
	char	*olddefault = defaultvalue;
	defaultvalue = newdefault;
	return olddefault;
}

/* Return the value of a jx_t.  If given NULL, then it returns the default
 * value as set by jx_default_text().
 */
char *jx_text(jx_t *json)
{
	if (!json)
		return defaultvalue;
	/* Maybe complain here if given an object, array, or key? */
	return json->text;
}

/* Return the value of a number as a double */
double jx_double(jx_t *json)
{
	if (!json || json->type != JX_NUMBER)
		return -1.0;
	if (json->text[0] == '\0' && json->text[1] == 'i')
		return (double)JX_INT(json);
	if (json->text[0] == '\0' && json->text[1] == 'd')
		return JX_DOUBLE(json);
	return atof(json->text);
}

/* Return the value of a number as an int */
int jx_int(jx_t *json)
{
	if (!json || json->type != JX_NUMBER)
		return -1;
	if (json->text[0] == '\0' && json->text[1] == 'i')
		return JX_INT(json);
	if (json->text[0] == '\0' && json->text[1] == 'd')
		return (int)JX_DOUBLE(json);
	return atoi(json->text);
}
