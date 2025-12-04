/* genunparse.c */

/* This file is not meant to be compiled separately -- it is included in
 * xml.c, and compiled as part of that.  Hence the lack of #includes.
 */

/* The general idea here is: xml_unparse() sets up a state, and invokes
 * xml_unparse_helper() to generate all tags for the top-level of the jx_t
 * argument.  xml_unparse_helper() calls xml_unparse_tag() to generate each tag
 * with its arguments and (by recursively calling xml_unparse_helper()) contents.
 *
 * Also worth noting: XML doesn't have real arrays; generally they're
 * represented by repeatable tags.  If the data passed into xml_unparse()
 * involves any arrays, that's not a native XML thing! It's JSON's clue to
 * generate multiple tags.
 */

typedef struct {
	char		*buffer;	/* whole buffer, or NULL */
	size_t		len;		/* length of whole response */
	int		pretty;		/* boolean: pretty-printing? */
	int		tab;		/* width of each indentation level */
	int		indent;		/* indentation level, if pretty */
	char		*crlf;		/* newline string: CRLF or just LF */
	const char	*suffix;	/* value of attributeSuffix setting */
	size_t		suffixlen;	/* length of suffix, in bytes */
	char		*namebuf;	/* buffer for tagname +/- suffix */
	size_t		namesize;	/* allocated size of namebuf */
} xml_unparse_state_t;


static void xml_unparse_helper(jx_t *data, xml_unparse_state_t *state);


/* Add text, without converting special characters to entities */
static void xml_unparse_add(const char *str, xml_unparse_state_t *state)
{
	if (state->buffer)
		strcpy(state->buffer + state->len, str);
	state->len += strlen(str);
}

/* Add text, converting special characters to entities */
static void xml_unparse_add_text(const char *str, xml_unparse_state_t *state)
{
	char *entity;
	for (; *str; str++) {
		/* Does this character need to be an entity? */
		switch (*str) {
		case '"':	entity = "&quot;";	break;
		case '<':	entity = "&lt;";	break;
		case '>':	entity = "&gt;";	break;
		case '&':	entity = "&amp;";	break;
		default:	entity = NULL;
		}
		if (entity) {
			/* Add the entity */
			xml_unparse_add(entity, state);
		} else {
			if (state->buffer)
				state->buffer[state->len] = *str;
			state->len++;
		}
	}
}

/* Generate an opening tag with a given name and attributes. */
static void xml_unparse_tag(const char *tagname, jx_t *attributes, jx_t *content, xml_unparse_state_t *state)
{
	jx_t	*attr;
	char	*str, *entity;
	char	*dup;

	/* If attributes is a non-object or empty, ignore it */
	if (attributes && (attributes->type != JX_OBJECT || !attributes->first))
		attributes = NULL;

	/* If content is null, ignore it */
	if (content && content->type == JX_NULL)
		content = NULL;

	/* Add the indentation, if "pretty" */
	if (state->pretty && state->indent > 0) {
		int indent = state->indent * state->tab;
		int i;
		if (state->buffer) {
			for (i = 0; i < indent; i++)
				state->buffer[state->len++] = ' ';
		} else
			state->len += indent;
	}

	/* Add the "<" and tag name */
	xml_unparse_add("<", state);
	xml_unparse_add(tagname, state);

	/* If there are attributes, output them */
	if (attributes) {
		for (attr = attributes->first; attr; attr = attr->next) {
			/* Skip the the value is false or null */
			if (!attr->first)
				continue;
			if (attr->first->type == JX_NULL)
				continue;
			if (attr->first->type == JX_BOOLEAN && attr->first->text[0] == 'f')
				continue;

			/* Add the name of the attribute */
			xml_unparse_add(" ", state);
			xml_unparse_add(attr->text, state);

			/* If the value is true then we're done */
			if (attr->first->type == JX_BOOLEAN)
				continue;

			/* Add =" before the value */
			xml_unparse_add("=\"", state);

			/* Get the value to add.  If it's a string (or a number
			 * that's stored in string form), this is easy but
			 * other values must be converted.
			 */
			if (attr->first->type == JX_STRING\
			 || (attr->first->type == JX_NUMBER && attr->first->text[0]))
				xml_unparse_add_text(attr->first->text, state);
			else {
				str = jx_serialize(attr->first, NULL);
				xml_unparse_add_text(str, state);
				free(str);
			}

			/* Add a terminating " */
			xml_unparse_add("\"", state);
		}
	}

	/* Is there content? */
	if (content && *tagname != '?') {
		/* Yes, so end this tag with ">".  If pretty-printing and the
		 * content is an object (embedded tags) then add a "\n"
		 */
		xml_unparse_add(">", state);
		if (state->pretty && content->type == JX_OBJECT) 
			xml_unparse_add(state->crlf, state);

		dup = NULL;
		if (content->type == JX_OBJECT) {
			/* Generate the content, using a higher indentation
			 * level.  If the tag name resides in state->namebuf
			 * then make a local copy of it because state->namebuf
			 * is likely to be overwritten.
			 */
			if (tagname == state->namebuf)
				dup = strdup(tagname);
			state->indent++;
			xml_unparse_helper(content, state);
			state->indent--;

			/* If pretty-printing, then indent the closing tag */
			if (state->pretty && state->indent > 0) {
				int indent = state->indent * state->tab;
				int i;
				if (state->buffer) {
					for (i = 0; i < indent; i++)
						state->buffer[state->len + i] = ' ';
				}
				state->len += indent;
			}
		} else {
			/* Simple value.  Add the string form of it */
			if (content->type == JX_STRING
			 || (content->type == JX_NUMBER && content->text[0])
			 || content->type == JX_BOOLEAN)
				xml_unparse_add_text(content->text, state);
			else {
				str = jx_serialize(content, NULL);
				xml_unparse_add_text(str, state);
				free(str);
			}
		}

		/* Generate the closing tag. */
		xml_unparse_add("</", state);
		xml_unparse_add(dup ? dup : tagname, state);
		xml_unparse_add(">", state);
		if (dup)
			free(dup);
	} else {
		/* No so end the opening tag with " />" or " ?>" */
		if (*tagname == '?')
			xml_unparse_add(" ?>", state);
		else
			xml_unparse_add(" />", state);
	}

	/* If pretty-printing, add a newline */
	if (state->pretty || *tagname == '?')
		xml_unparse_add(state->crlf, state);
}

/* Test whether a given member name looks like attributes instead of content */
static int xml_is_attributes(const char *name, xml_unparse_state_t *state)
{
	size_t len = strlen(name);
	if (state->suffixlen >= len)
		return 0;
	if (strcmp(name + len - state->suffixlen, state->suffix))
		return 0;
	return 1;
}

/* Make a copy of a tagname.  If suffix=1 then add suffix, if suffix=-1 then
 * delete suffix, for suffix=0 no changes.
 */
static void xml_unparse_name(const char *name, xml_unparse_state_t *state, int suffix)
{
	/* Get the length of the maybe-altered name */
	size_t newlen = strlen(name) + suffix * state->suffixlen;

	/* If too big for the current namebuf, then enlarge it */
	if (newlen + 1 > state->namesize) {
		state->namesize = (newlen | 0x1f) + 1;
		state->namebuf = (char *)realloc(state->namebuf, state->namesize);
	}

	/* Copy the name into namebuf, and adjust it */
	strncpy(state->namebuf, name, newlen);
	state->namebuf[newlen] = 0;
	if (suffix == 1)
		strcat(state->namebuf, state->suffix);
}

/* This file converts a jx_t object to an XML string.  Returns (in "state")
 * the size of the string in bytes, not counting the terminating '\0' byte.
 * The string itself is stored at "buf", but you can also pass a NULL "buf"
 * to find the length without actually generating it.
 */
static void xml_unparse_helper(jx_t *data, xml_unparse_state_t *state)
{
	size_t	len, piece_len;
	jx_t	*mem, *attr, *mscan, *ascan;

	assert(data->type == JX_OBJECT);

	/* For each member of the object... */
	for (mem = data->first; mem; mem = mem->next) {
		/* Is it <!tag...>? */
		if (mem->text[0] == '!') {
			/* The tag contents are normally stored as a string,
			 * or an an array of strings for repeated tags.
			 * Ignore any other values.
			 */
			if (!mem->first)
				continue;
			if (mem->first->type == JX_STRING) {
				xml_unparse_add("<", state);
				xml_unparse_add(mem->text, state);
				xml_unparse_add(" ", state);
				xml_unparse_add(mem->first->text, state);
				xml_unparse_add(">", state);
				xml_unparse_add(state->crlf, state);
			} else if (mem->first->type == JX_ARRAY) {
				for (ascan = jx_first(mem->first);
				     ascan;
				     ascan = jx_next(ascan)) {
					if (ascan->type != JX_STRING)
						continue;
					xml_unparse_add("<", state);
					xml_unparse_add(mem->text, state);
					xml_unparse_add(" ", state);
					xml_unparse_add(ascan->text, state);
					xml_unparse_add(">", state);
					xml_unparse_add(state->crlf, state);
				}
			}
			continue;
		}

		/* Is it an attribute bundle? */
		if ((mem->first->type == JX_OBJECT || mem->first->type == JX_ARRAY) && xml_is_attributes(mem->text, state)) {
			/* If there's a non-attribute version, let that handle it */
			xml_unparse_name(mem->text, state, -1);
			if (jx_by_key(data, state->namebuf))
				continue;

			/* Okay, attributes are all we've got.  Generate an
			 * empty tag with the attributes.
			 */
			xml_unparse_tag(state->namebuf, mem->first, NULL, state);
			continue;
		}

		/* IF WE GET HERE, WE HAVE A "NORMAL" TAG */

		/* Look for attributes */
		xml_unparse_name(mem->text, state, 1);
		attr = jx_by_key(data, state->namebuf);

		/* The value could be an array, or a single item. */
		if (mem->first->type == JX_ARRAY) {
			/* attr, if used, should also be an array */
			if (attr && attr->type != JX_ARRAY)
				attr = NULL;

			/* Loop over the array, adding tags */
			for (mscan = jx_first(mem->first), ascan = jx_first(attr);
			     mscan;
			     mscan = jx_next(mscan), ascan = jx_next(ascan)) {
				xml_unparse_tag(mem->text, ascan, mscan, state);
			}
		} else {
			/* Single value. attr, if used, should also be single */
			if (attr && attr->type == JX_ARRAY)
				attr = NULL;

			/* Add the tag */
			xml_unparse_tag(mem->text, attr, mem->first, state);
		}
	}
}

static size_t xml_unparse(char *buf, jx_t *data)
{
	xml_unparse_state_t state;

	/* Set up the state */
	state.buffer = buf;
	state.len = 0;
	state.pretty = jx_config_get_boolean(NULL, "pretty");
	state.tab = jx_config_get_int(NULL, "tab");
	state.indent = 0;
	state.crlf = jx_config_get_boolean("plugin.xml", "generateCRLF") ? "\r\n" : "\n";
	state.suffix = jx_config_get_text("plugin.xml", "attributeSuffix");
	state.suffixlen = strlen(state.suffix);
	state.namesize = 128;
	state.namebuf = (char *)malloc(state.namesize);

	/* Let the helper do most of the work (recursively) */
	xml_unparse_helper(data, &state);

	/* Clean up, and return the length */
	if (state.pretty)
		state.len -= strlen(state.crlf);
	if (buf)
		buf[state.len] = '\0';
	free(state.namebuf);
	return state.len;
}
