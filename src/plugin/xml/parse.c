/* parse.c */

/* This file is not meant to be compiled separately -- it is included in
 * xml.c, and compiled as part of that.  Hence the lack of #includes.
 */

/*
General strategy for the parser: an xml_parse() function parses the contents
of an XML tag pair.  The contents will be either empty, or a single chunk of
text, or a series of nested tag pairs.  If empty, that becomes either an
empty string, empty object, or empty array depending on the "empty" setting.
If it is text then it is returned as a string, unless it looks like a number
and the "parseNumber" option is set in which case it's returned as a number.
If it is a series of tags then they'll be returned as an object.  Repeated
tags are represented as an array.  Tag attributes are returned as separate
members with the "attributeSuffix" option appended to the tag name.

If an error is detected then an error null is returned.

The top level parser function just does some basic setup and then calls
the parser function to (hopefully) return an object containing a member
with the same name as the outermost XML tag.

*/

static char *xml_parse_attr_suffix;

/* Replace all known entities with their corresponding text, and return the
 * size of the resulting text in bytes, not counting the terminating '\0'
 * (which is does add).  If buf is null, then just return the size.
 */
static size_t xml_entities_to_plain(char *buf, const char *str, size_t len)
{
	const char *max;	/* end of the string */
	json_t	*entity, *found;
	wchar_t	wc;	/* used for &#nnnn; and &#xhhhh; */
	char	*name;
	size_t	namesize;
	size_t	namelen;
	char	dummy[MB_CUR_MAX];
	int	out;
	const char *start;

	/* Find the end of the string */
	if (len == (size_t)-1)
		max = str + strlen(str);
	else
		max = str + len;

	/* Locate the object containing entity translations */
	entity = json_by_expr(json_config, "plugin.xml.entity", NULL);

	/* Allocate the initial name buffer */
	namesize = 100;
	name = (char *)malloc(namesize);

	/* Copy most characters literally.  When we hit '&', get clever! */
	for (len = 0; *str && str < max; str++) {
		/* Normal characters, or bytes in normal UTF-8 characters */
		if (*str != '&') {
			if (buf)
				buf[len] = *str;
			len++;
			continue;
		}

		/* Okay, it's an entity. Let's copy it into "name" */
		start = str++;
		for (namelen = 0; &str[namelen] < max && str[namelen] && strchr(";<> \n", str[namelen]); namelen++) {
			if (namelen >= namesize - 1) {
				namesize += 100;
				name = (char *)realloc(name, namesize);
			}
			name[namelen] = str[namelen];
		}
		if (*str == ';')
			str++;
		name[namelen] = '\0';

		/* Decimal?  Hex? */
		if (name[0] == '#') {
			/* Convert to a Unicode codepoint */
			if (name[1] == 'x' || name[1] == 'X')
				wc = (wchar_t)strtol(name + 2, NULL, 16);
			else
				wc = (wchar_t)atol(name + 1);

			/* If outside of Unicode range, convert to '?' */
			if (wc < 1 || wc >= 0x101000)
				wc = '?';

			/* Convert to UTF-8 */
			if (buf) {
				out = wctomb(buf + len, wc);
			} else {
				out = wctomb(dummy, wc);
			}
			len += out;
			continue;
		}

		/* Named entity.  Look it up */
		found = json_by_key(entity, name);
		if (found) {
			/* Use its value.  It could be a string or number */
			if (found->type == JSON_STRING) {
				if (buf)
					strcpy(buf + len, found->text);
				len += strlen(found->text);
				continue;
			} else if (found->type == JSON_NUMBER) {
				wc = (wchar_t)json_int(found);
				if (buf) {
					out = wctomb(buf + len, wc);
				} else {
					out = wctomb(dummy, wc);
				}
				len += out;
				continue;
			}
		}

		/* No joy.  Just leave it unchanged */
		if (buf)
			buf[len] = '&';
		len++;
		str = start + 1;
	}

	/* end it */
	if (buf)
		buf[len] = '\0';
	return len;
}

/* Parse a tag, including its contents and closing tag.  We want to return...
 *  1) The position after the end of the closing tag.
 *  2) The name of the tag.
 *  3) The attributes, as an object.  NULL if none.
 *  4) The contents, either an object or a string.
 *  5) An error, with both an error symbol+message and a position.
 * This function is recursive.
 */
static json_t *xml_parse_tag(const char **refstr, char **reftagname, json_t **refattr)
{
	json_t	*attr, *content, *result, *array, *attrarray, *tmp;
	char	*tagname, *tagattr;
	size_t	len, taglen;
	int	arraylen;

	/* Clobber the pointed-to variables */
	*reftagname = NULL;
	*refattr = NULL;

	/* Skip whitespace */
	while (isspace(**refstr))
		(*refstr)++;

	/* If not a tag, then return an error */
	if (**refstr != '<')
		return json_error_null(*refstr, "XMLNOTTAG:XML expects a \"<\" here");

	/* If ending tag, that's an error */
	(*refstr)++;
	if (**refstr == '/')
		return json_error_null(*refstr, "XMLNEST:Bad nesting of XML tags");

	/* Don't allow empty namespaces */
	if (**refstr == '/')
		return json_error_null(*refstr, "XMLEMPTYNS:Empty XML namespace");

	/* Find the length of the tag name, and copy it */
	for (taglen = 0; (*refstr)[taglen] && !strchr(" \t\r\n/>", (*refstr)[taglen]); taglen++) {
	}
	*reftagname = (char *)malloc(taglen + 1);
	strncpy(*reftagname, *refstr, taglen);
	(*reftagname)[taglen] = '\0';
	(*refstr) += taglen;

	/* Skip whitespace after the tag name */
	while (isspace(**refstr))
		(*refstr)++;
	if (!**refstr)
		return json_error_null((*refstr) - 1, "XMLSHORT:Premature end to XML text");

	/* If the tagname starts with "?" or "!" then parse the attribute area
	 * as a value string.
	 */
	if (**reftagname == '?' || **reftagname == '!') {
		for (len = 0; (*refstr)[len] && (*refstr)[len] != '>'; len++) {
		}
		content = json_string(*refstr, len);
		(*refstr) += len;
		if (**refstr == '>')
			(*refstr)++;
		return content;
	}

	/* Skip whitespace */
	while (isspace(**refstr))
		(*refstr)++;

	/* If any attributes, build an object for them */
	if (**refstr != '>' && **refstr != '/') {
	/*!!!*/
	}

	/* Move past the ">".  "/>" then assume empty content */
	if (**refstr == '/') {
		(*refstr)++;
		if (**refstr != '>')
			return json_error_null(*refstr, "XMLSLASH:XML tag contains / in the wrong place");
		(*refstr)++;
		return NULL;
	}

	/* Skip whitespace */
	while (isspace(**refstr))
		(*refstr)++;

	/* Do we have string content, or nested tags? */
	result = NULL;
	if (**refstr == '<') {
		/* Zero or more sets of nested tags */
		while ((*refstr)[1] != '/') {
			/* Parse a nested tag, recursively */
			content = xml_parse_tag(refstr, &tagname, &attr);

			/* If error, return that */
			if (json_is_error(content)) {
				if (content)
					json_free(content);
				return content;
			}

			/* If no content, then use an empty string, array or object */
			if (!content) {
				content = json_string("", 0); /* !!! */
			}

			/* Derive a name for attributes */
			tagattr = (char *)malloc(strlen(tagname) + strlen(xml_parse_attr_suffix) + 1);
			strcpy(tagattr, tagname);
			strcat(tagattr, xml_parse_attr_suffix);

			/* Add it to the result object.  If duplicate then
			 * make it an array.
			 */
			if (!result) {
				result = json_object();
				if (attr)
					json_append(result, json_key(tagattr, attr));
				json_append(result, json_key(tagname, attr));
			} else if ((array = json_by_key(result, tagname)) != NULL) {
				/* Duplicate. If not already an array, then
				 * convert it now.  Also convert the attributes
				 * to a parallel array, if any.
				 */
				attrarray = json_by_key(result, tagattr);
				if (array->type != JSON_ARRAY) {
					/* Convert previous content to array */
					tmp = json_array();
					json_append(tmp, array);
					json_append(result, json_key(tagname, tmp));
					array = tmp;

					/* If attributes, convert them too */
					if (attrarray) {
						tmp = json_array();
						json_append(tmp, attrarray);
						json_append(result, json_key(tagattr, tmp));
						attrarray = tmp;
					}
				}

				/* Append this item */
				json_append(array, content);

				/* Append attributes to keep the arrays parallel */
				if (attr && attrarray)
					json_append(array, attr);
				else if (attrarray)
					json_append(array, json_object());
				else if (attr) {
					attrarray = json_array();
					for (arraylen = json_length(array); arraylen > 1; arraylen--)
						json_append(attrarray, json_object());
					json_append(attrarray, attr);
					json_append(result, json_key(tagattr, tmp));
				}

			} else {
				/* New member for the object */
				if (attr)
					json_append(result, json_key(tagattr, attr));
				json_append(result, json_key(tagname, attr));
			}

			/* Discard tagname */
			free(tagattr);

			/* Skip whitespace between tags */
			while (isspace(**refstr))
				(*refstr)++;

			/* If we hit text other than "<" thats an error */
			if (**refstr != '<')
				return json_error_null(*refstr, "XMLMIX:Mixture of text and XML tags");
		}
	} else {
		/* Find the extent of the text. */

		/* Convert it to a jsoncalc string */

		/* Add it to the object */
	}

	/* We expect a closing tag here.  If it is an opening that, that's
	 * the result of mixing text and tags, which is bad.  If it's the wrong
	 * closing tag, that could be an error or it could be HTML.
	 */
	if (**refstr != '<')
		return json_error_null(*refstr, "XMLNOTTAG:XML expects a \"<\" here");
	(*refstr)++;
	if (**refstr != '/')
		return json_error_null(*refstr, "XMLMIX:Mixture of text and XML tags");
	(*refstr)++;
	if (strncmp(*refstr, *reftagname, taglen) != 0
	 || (*refstr)[taglen] != '>')
		return json_error_null(*refstr, "XMLNEST:Bad nesting of XML tags");
	(*refstr) += taglen + 1; /* "+1" for the ">" after the tag name */

	/* Done! */
	return result;
}

/*----------------------------------------------------------------------------*/

/* Return 1 if "str" appears to be XML, else return 0 */
static int xml_test(const char *str, size_t len)
{
	return *str == '<';
}

/* Parse "str" as XML and return it as a JSON object.  Store a pointer to the
 * end of the parsed text at "refend" unless "refend" is NULL.  If an error
 * is detected, store a pointer to the location of the error at "referr" (if
 * "referr" is not NULL) and return a json_t containing an error null.
 */
static json_t *xml_parse(const char *buf, size_t len, const char **refend, const char **referr)
{
	json_t *result;
	json_t *content, *attributes;
	char *tagname;

	/* Copy some values from config to variables */
	attributes = json_by_expr(json_config, "plugin.xml.attributeSuffix", NULL);
	xml_parse_attr_suffix = attributes->text;

	result = json_object();

	for (;;) {
		/* Skip whitespace */

		/* Parse a tag */
		//xml_parse_tag(const char **refstr, char **reftagname, json_t **refattr)
	}
}


