/* parse.c */

/* This file is not meant to be compiled separately -- it is included in
 * xml.c, and compiled as part of that.  Hence the lack of #includes.
 */

/* General strategy for the parser: The xml_parse() function sets up a
 * "xml_parse_state_t" struct, and then passes control to xml_parse_helper().
 * xml_parse_helper() parses a series of tags and collects them in an object,
 * which it returns.  It uses an xml_parse_tag() function to parse a single
 * tag including its attributes, contents, and closing tag.  To parse the
 * contents it may recursively call xml_parse_helper().
 *
 * If an error is detected, NULL is returned and the state will include the
 * position where detection occurred.
 *
 * The xml_entities_to_plain() function replaces entities with their
 * corresponding text.
 */

typedef struct xml_parse_name_stack_s {
	struct xml_parse_name_stack_s *pop;
	char *namedup;
} xml_parse_name_stack_t;

typedef struct {
	const char	*cursor;/* current parse position */
	const char	*end;	/* End of the text */
	int	parseNumber;	/* Boolean: parse digit strings as numbers? */
	int	strictPair;	/* Boolean: require <tag> to have a </tag>? */
	const char *empty;	/* One of "string", "object", or "array" */
	const char *suffix;	/* Value of the attributeSiffix setting */
	size_t	suffixlen;	/* length of the suffix, in bytes */
	char	*err;		/* error message (static literal) */
	char	*name;		/* buffer for storing tag/attribute name */
	size_t	namesize;	/* size of the buffer */
	xml_parse_name_stack_t *stack;
	char	*attrname;	/* Buffer for holding name + suffix */
} xml_parse_state_t;

/* This is used to return a single tag.  The tag's name will be available
 * in state->name.
 */
typedef struct {
	jx_t	*attributes;	/* Object with attributes, or NULL if none */
	jx_t	*content;	/* Contents: string, number, object, or NULL */
} xml_parse_tag_t;

static jx_t *xml_parse_helper(xml_parse_state_t *state);

/* Replace all known entities with their corresponding text, and return the
 * size of the resulting text in bytes, not counting the terminating '\0'
 * (which is does add).  If buf is null, then just return the size.
 */
static size_t xml_entities_to_plain(char *buf, const char *str, size_t len)
{
	const char *max;	/* end of the string */
	jx_t	*entity, *found;
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
	entity = jx_config_get("plugin.xml", "entity");

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
		found = jx_by_key(entity, name);
		if (found) {
			/* Use its value.  It could be a string or number */
			if (found->type == JX_STRING) {
				if (buf)
					strcpy(buf + len, found->text);
				len += strlen(found->text);
				continue;
			} else if (found->type == JX_NUMBER) {
				wc = (wchar_t)jx_int(found);
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


/* Push the current name onto the name stack */
static void xml_parse_push_name(xml_parse_state_t *state)
{
	xml_parse_name_stack_t *s;
	s = (xml_parse_name_stack_t *)malloc(sizeof(*s));
	s->pop = state->stack;
	s->namedup = strdup(state->name);
	state->stack = s;
}

/* Pop a name from the name stack, loading it back into state->name */
static void xml_parse_pop_name(xml_parse_state_t *state)
{
	assert(state->stack);
	xml_parse_name_stack_t *pop = state->stack->pop;
	strcpy(state->name, state->stack->namedup); /* guaranteed to fit */
	free(state->stack->namedup);
	free(state->stack);
	state->stack = pop;
}

/* Return the type of JSON value that an empty tag pair represents */
static jx_t *xml_parse_empty(xml_parse_state_t *state)
{
	switch (state->empty[0]) {
	case 'o':	return jx_object();
	case 'a':	return jx_array();
	case 'n':	return jx_null();
	default:	return jx_string("",0);
	}
}

/* Move past whitespace */
static void xml_parse_space(xml_parse_state_t *state)
{
	while (state->cursor < state->end && isspace(*state->cursor))
		state->cursor++;
}

/* Parse a single name.  This could be a tag name or attribute name */
static void xml_parse_name(xml_parse_state_t *state)
{
	size_t	len;

	/* Count the name characters */
	for (len = 0; (state->cursor[len] & 0xff) > ' ' && state->cursor[len] != '=' && state->cursor[len] != '>'; len++) {
	}

	/* Copy it into namebuf, with a '\0' terminator */
	if (len + 1 > state->namesize) {
		state->namesize = (len | 0x1f) + 1;
		state->name = (char *)realloc(state->name, state->namesize);
		state->attrname = (char *)realloc(state->attrname, state->namesize + state->suffixlen);
	}
	if (len > 0)
		strncpy(state->name, state->cursor, len);
	state->name[len] = '\0';

	/* Move the cursor past the name.  Skip trailing spaces */
	state->cursor += len;
	xml_parse_space(state);
}

/* Parse a single tag.  Recursively call xml_parse_helper to parse the contents.
 * If an error is detected, the state->err field will contain an error message.
 */
static xml_parse_tag_t xml_parse_tag(xml_parse_state_t *state)
{
	xml_parse_tag_t ret = {NULL, NULL};
	char	type;
	int	nest;
	size_t	len;
	jx_t	*value;
	const char	*oldcursor;

	/* We should be at the start of a tag */
	assert(*state->cursor == '<');

	/* Parse the name.  The name may start with "?" or "!" which is handled
	 * differently than normal tags.  If it starts with "/" then that's
	 * something else.
	 */
	state->cursor++;
	xml_parse_name(state);

	/* Parse <!...> as one big content string. */
	if (*state->name == '!') {
		for (nest = 0, len = 0; nest > 0 || state->cursor[len] != '>'; len++) {
			if (state->cursor[len] == '<')
				nest++;
			else if (state->cursor[len] == '>')
				nest--;
		}

		/* Return it.  The name is in state->name, there are no
		 * attributes, and the content is a string.
		 */
		ret.content = jx_string(state->cursor, len);
		state->cursor += len + 1;
		return ret;
	}

	/* If we get </...> that's an error. */
	if (*state->name == '/') {
		state->err = "Unmatched </tag>";
		return ret;
	}

	/* Otherwise, for <tag> or <?tag...?>, parse the attributes */
	xml_parse_push_name(state);
	xml_parse_space(state);
	while (state->cursor < state->end && !strchr("?>/", *state->cursor)) {
		/* Parse a name */
		xml_parse_name(state);

		/* Is it followed by "="? */
		xml_parse_space(state);
		if (*state->cursor == '=') {
			/* Yes, parse the value.  It should be quoted but if
			 * not then assume it ends at whitespace or tag end.
			 */
			char *plaintext, *end;
			size_t plainlen;
			state->cursor++;
			if (*state->cursor == '"') {
				/* Quoted */
				state->cursor++;
				for (len = 0; state->cursor[len] != '"'; len++) {
				}
				plainlen = xml_entities_to_plain(NULL, state->cursor, len);
				value = jx_string("", plainlen);
				(void)xml_entities_to_plain(value->text, state->cursor, len);
				state->cursor += len + 1;
			} else {
				/* Unquoted */
				for (len = 0; !isspace(state->cursor[len]) && !strchr("/?>", state->cursor[len]); len++) {
				}
				value = jx_string(state->cursor, len);
				state->cursor += len;
			}

		} else {
			/* assume it is Boolean "true" */
			value = jx_boolean(1);
		}

		/* Add this to the attributes object. If this is the first then
		 * we also need to allocate the object.
		 */
		if (!ret.attributes)
			ret.attributes = jx_object();
		jx_append(ret.attributes, jx_key(state->name, value));

		/* Skip whitespace */
		xml_parse_space(state);
	}
	xml_parse_pop_name(state);

	/* If we hit ?> or /> then there is no content.  This is also true if
	 * the tag name starts with "?" even if the tag doesn't end with "?>".
	 */
	if (*state->cursor == '/' || *state->cursor == '?') {
		state->cursor++;
		if (*state->cursor++ == '>') {
			ret.content = xml_parse_empty(state);
			return ret;
		}
		state->err = "malformed tag";
		if (ret.attributes) {
			jx_free(ret.attributes);
			ret.attributes = NULL;
		}
		return ret;
	}
	if (*state->name == '?') {
		ret.content = xml_parse_empty(state);
		return ret;
	}
	state->cursor++;
	xml_parse_space(state);

	/* Parse the content.  Detect errors. */
	xml_parse_push_name(state);
	ret.content = xml_parse_helper(state);
	xml_parse_pop_name(state);
	if (!ret.content) {
		if (ret.attributes) {
			jx_free(ret.attributes);
			ret.attributes = NULL;
		}
		return ret;
	}
{char *tmp = jx_serialize(ret.content,0); fprintf(stderr, "%s=%s\n", state->name, tmp); free(tmp); }
{char *tmp = ret.attributes ? jx_serialize(ret.attributes,0) : strdup("null"); fprintf(stderr, "%s=%s\n", state->attrname, tmp); free(tmp); }

	/* We expect to be at a closing tag.  If not, either that's an error. */
	if (*state->cursor != '<' || state->cursor[1] != '/') {
		if (ret.attributes) {
			jx_free(ret.attributes);
			ret.attributes = NULL;
		}
		jx_free(ret.content);
		ret.content = NULL;
		state->err = "Parse error";
		return ret;
	}

	/* Check whether we're at the expected closing tag.  If not, that's
	 * either an error or we keep it as the some other tag's closer.
	 */
	oldcursor = state->cursor;
	state->cursor += 2;
	strcpy(state->attrname, state->name);
	xml_parse_name(state);
	if (strcmp(state->attrname, state->name)) {
		if (state->strictPair) {
			if (ret.attributes) {
				jx_free(ret.attributes);
				ret.attributes = NULL;
			}
			jx_free(ret.content);
			ret.content = NULL;
			state->err = "Mismatched <tag>...</tag>";
			return ret;
		}
		state->cursor = oldcursor;
	}
	xml_parse_space(state);
	if (*state->cursor == '>') {
		state->cursor++;
		xml_parse_space(state);
	} else {
		if (ret.attributes) {
			jx_free(ret.attributes);
			ret.attributes = NULL;
		}
		jx_free(ret.content);
		ret.content = NULL;
		state->err = "xmlclose:Unexpected chars in </tag>";
	}

	/* Done! */
	return ret;
}

/* Parse the content of a tag.  This may be either a string/number value, or
 * an empty string/object/array, or one or more nested tags to be collected
 * as a JSON object.  Whatever it is, return it.
 *
 * This is also called as the top-level parser for an XML document.  Typically
 * this will end up parsing the <?xml ... ?> tag and a single document tag
 * that has lots of stuff inside.
 *
 * It returns NULL on errors, or if it hits the end of the buffer without
 * finding anything to parse.
 */
static jx_t *xml_parse_helper(xml_parse_state_t *state)
{
	jx_t	*parsed, *attr, *content, *scan;
	xml_parse_tag_t tag;
	size_t	len, entitylen;

	/* Skip leading whitespace */
	xml_parse_space(state);
	if (state->cursor >= state->end)
		return NULL;

	/* If not "<" then we have a string/number value */
	if (*state->cursor != '<') {
		/* Count characters */
		for (len = 1; state->cursor + len < state->end && state->cursor[len] != '<'; len++) {
		}

		/* Trim trailing whitespace */
		while (len > 0 && isspace(state->cursor[len - 1]))
			len--;


		/* Expand entities, and store it in a string */
		entitylen = xml_entities_to_plain(NULL, state->cursor, len);
		parsed = jx_string("", entitylen);
		(void)xml_entities_to_plain(parsed->text, state->cursor, len);

		/* Are we supposed to parse numbers? */
		if (state->parseNumber) {
			/* Does it look like a number? */
			char *n = parsed->text;
			if (*n == '-')
				n++;
			if (isdigit(*n)) {
				do {
					n++;
				} while (isdigit(*n));
				if (*n == '.' && isdigit(n[1])) {
					do {
						n++;
					} while (isdigit(*n));
					if (!*n) {
						/* Yes, it looks like a number!
						 * Convert to number.
						 */
						parsed->type = JX_NUMBER;
					}
				}
			}
		}

		/* Move cursor past the value */
		state->cursor += len;

		/* Return the string/number */
		return parsed;
	}

	/* WE HAVE TAGS (or a closing tag) */

	/* If "</" then we hit an ending tag without any content.  Choose the
	 * proper type of empty value to return.  This will leave state->cursor
	 * pointing to the start of the closing tag.
	 */
	if (state->cursor[1] == '/') {
		return xml_parse_empty(state);
	}

	/* We found a nested tag.  Parse it and its content. May be repeated. */
	parsed = jx_object();
	do {
{char *tmp = jx_serialize(parsed,0); fprintf(stderr, "%s\n", tmp); free(tmp); }
		/* Parse a tag */
		tag = xml_parse_tag(state);

		/* Detect errors */
		if (state->err) {
			jx_free(parsed);
			return NULL;
		}

		/* Add it to the object.  This is more complicated than it
		 * sounds because...
		 *  1) Duplicate tags are converted to JSON arrays.
		 *  2) We always have content.  We sometimes have attributes.
		 *  3) Attribute arrays should parallel content arrays.
		 */

		/* Derive the attribute name from the tag name */
		if (tag.attributes) {
			strcpy(state->attrname, state->name);
			strcat(state->attrname, state->suffix);
		}

		/* Look for existing members for this tag, both for the
		 * attributes and the content.  We don't use jx_by_key()
		 * for this because XML is case-sensitive.  Also, this loop
		 * finds the JX_KEY nodes, which work better for us than
		 * the values.
		 */
		for (attr = content = NULL, scan = parsed->first;
		     scan && ((tag.attributes && !attr) || !content);
		     scan = scan->next) {
			if (tag.attributes && !strcmp(scan->text, state->attrname))
				attr = scan;
			else if (!strcmp(scan->text, state->name))
				content = scan;
		}

		/* NOTE: At this point, "content" and "attr" refer to JX_KEY
		 * nodes in the parsed data, or are NULL if no such nodes exist.
		 * "tag.content" and "tag.attributes" are values (not keys)
		 * from the current tag.  "tag.content" will never be NULL,
		 * but "tag.attributes" might be.
		 */

		/* If content was NOT found, then just add it to the object
		 * as a non-array value.
		 */
		if (!content) {
			jx_append(parsed, jx_key(state->name, tag.content));
			if (tag.attributes)
				jx_append(parsed, jx_key(state->attrname, tag.attributes));
			xml_parse_space(state);
			continue;
		}

		/* A member was found for this tag's content already, so we
		 * must be doing arrays.  If the existing content isn't an
		 * array yet, then convert it to an array now.
		 */
		if (content->first->type != JX_ARRAY) {
			jx_t *array = jx_array();
			jx_append(array, content->first);
			content->first = array;
		}

		/* Same for attributes.  It's also possible that no attributes
		 * have been seen before this, though, in which case we need
		 * to add it as an array.  Either way, we need to pad it to
		 * be the same length as the content array.
		 */
		if (tag.attributes && (!attr || attr->first->type != JX_ARRAY)) {
			jx_t *array = jx_array();
			int pad;
			if (attr) {
				jx_append(array, attr->first);
				attr->first = array;
			} else {
				attr = jx_key(state->attrname, array);
				jx_append(parsed, attr);
			}
			pad = jx_length(content->first) - jx_length(array);
			for (; pad > 0; pad--)
				jx_append(array, jx_object());
		}

		/* Append the new content to the array. */
		jx_append(content->first, tag.content ? tag.content : jx_null());

		/* Do the same for attributes.  If no attributes, but there
		 * is an attribute array, then append an empty object instead.
		 */
		if (attr)
			jx_append(attr->first, tag.attributes ? tag.attributes  : jx_object());


		/* Skip whitespace */
		xml_parse_space(state);

	} while (state->cursor + 2 < state->end
	      && state->cursor[0] == '<'
	      && state->cursor[1] != '/');

	/* Okay!  We had some fun, parsed some tags, and hit a closing tag or
	 * the end of the buffer.  The state->cursor points to the start of
	 * that closing tag or end of the buffer.  Return what we parsed.
	 */
	return parsed;
}

/*----------------------------------------------------------------------------*/

/* The following two functions are passed to jx_parse_hook() to allow XML
 * data to be recognized and parsed.
 */

/* Return 1 if "str" appears to be XML, else return 0 */
static int xml_test(const char *str, size_t len)
{
	return *str == '<';
}

/* Parse "str" as XML and return it as a JSON object.  Store a pointer to the
 * end of the parsed text at "refend" unless "refend" is NULL.  If an error
 * is detected, store a pointer to the location of the error at "referr" (if
 * "referr" is not NULL) and return a jx_t containing an error null.
 */
static jx_t *xml_parse(const char *buf, size_t len, const char **refend, const char **referr)
{
	jx_t *result;
	xml_parse_state_t state;

	/* Set up the parse state */
	state.cursor = buf;
	state.end = buf + len;
	state.parseNumber = jx_config_get_boolean("plugin.xml", "parseNumber");
	state.strictPair = jx_config_get_boolean("plugin.xml", "strictPair");
	state.empty = jx_config_get_text("plugin.xml", "empty");
	state.suffix = jx_config_get_text("plugin.xml", "attributeSuffix");
	state.suffixlen = strlen(state.suffix);
	state.err = NULL;
	state.namesize = 128;
	state.name = (char *)malloc(state.namesize);
	state.name[0] = '\0';
	state.attrname = (char *)malloc(state.namesize + state.suffixlen);
	state.attrname[0] = '\0';
	state.stack = NULL;

	/* Let xml_parse_helper to the dirty work */
	result = xml_parse_helper(&state);

	/* If NULL then we got an error. Convert NULL to an error message. */
	if (!result) {
		if (state.err)
			result = jx_error_null(state.cursor, "xml:%s", state.err);
		else
			result = jx_error_null(state.cursor, "xmlempty:No XML data");
	}

	/* Clean up and exit */
	free(state.name);
	while (state.stack) /* shouldn't be necessary, but do it anyway */
		xml_parse_pop_name(&state);
	return result;
}


