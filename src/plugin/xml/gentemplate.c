/* gentemplate.c */

/* This file is not meant to be compiled separately -- it is included in
 * xml.c, and compiled as part of that.  Hence the lack of #includes.
 */


/* This file converts a json_t object to an XML string.  Returns the size of
 * the string in bytes, not counting the terminating '\0' byte.  The string
 * itself is stored at "buf".  You can also pass a null "buf" to find the
 * length without actually generating it.
 */
static size_t xmlTemplate(char *buf, json_t *data, const char *template)
{
	return 0;
}

