#include <stdlib.h>
#include <stdio.h>
#include <jsoncalc.h>

static char *defaultvalue;

/* Set the default value for json_text, when no data is found.  Returns the
 * old default.
 */
char *json_default_text(char *newdefault)
{
	char	*olddefault = defaultvalue;
	defaultvalue = newdefault;
	return olddefault;
}

/* Return the value of a json_t.  If given NULL, then it returns the default
 * value as set by json_default_text().
 */
char *json_text(json_t *json)
{
	if (!json)
		return defaultvalue;
	/* Maybe complain here if given an object, array, or key? */
	return json->text;
}

/* Return the value of a number as a double */
double json_double(json_t *json)
{
	if (!json || json->type != JSON_NUMBER)
		return -1.0;
	if (json->text[0] == '\0' && json->text[1] == 'i')
		return (double)JSON_INT(json);
	if (json->text[0] == '\0' && json->text[1] == 'd')
		return JSON_DOUBLE(json);
	return atof(json->text);
}

/* Return the value of a number as an int */
int json_int(json_t *json)
{
	if (!json || json->type != JSON_NUMBER)
		return -1;
	if (json->text[0] == '\0' && json->text[1] == 'i')
		return JSON_INT(json);
	if (json->text[0] == '\0' && json->text[1] == 'd')
		return (int)JSON_DOUBLE(json);
	return atoi(json->text);
}
