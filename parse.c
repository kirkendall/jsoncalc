#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "json.h"

/* Here we need to access the "real" parse functions */
#ifdef JSON_DEBUG_MEMORY
# undef json_parse_string
#endif

/* allocate a json_t for a given token. */
json_t *json_simple_from_token(json_token_t *token)
{
        size_t len;
        json_t  *json;
        const char *s, *end;

        /* Null is easy.  We don't want the text "null" in it though */
        if (token->type == JSON_NULL)
		return json_null();

        /* Non-strings and empty strings are easy */
	if (token->type != JSON_STRING || token->len == 0)
                return json_simple(token->start, token->len, token->type);

        /* Most strings don't have character escapes (backslashes).
         * Optimize for that.
         */
	for (s = token->start, end = s + token->len; s != end; s++) {
		if (*s == '\\')
			break;
	}
	if (s == end)
                return json_simple(token->start, token->len, token->type);

        /* Handle character escapes */
        len = json_mbs_unescape(NULL, token->start, token->len);
        json = json_string("", len + 1);
        json_mbs_unescape(json->text, token->start, token->len);
        json->text[len] = '\0';
        return json;
}

static void jappendarray(json_t *container, json_t *more)
{
	json_t	*scan;

	if (!container->first) {
		/* First element */
		assert(JSON_END_POINTER(container) == NULL);
		container->first = more;
	} else if ((scan = JSON_END_POINTER(container)) != NULL) {
		/* Next element, optimized via JSON_POINTER_END() */
		assert(scan->next == NULL);
		scan->next = more;
	} else {
		/* Next element, unoptimized */
		for (scan = container->first; scan->next; scan = scan->next) {
		}
		scan->next = more;
	}
	JSON_END_POINTER(container) = more;
}

static void jappendobject(json_parse_t *state, json_t *container, json_t *more)
{
	json_t	*scan;

	if (!container->first) {
		container->first = more;
		JSON_END_POINTER(container) = more;
	} else if (state) {
		/* Use the end pointer to jump directly to the end.  This only
		 * works during parsing since other manipulations could make
		 * the end pointer be out of date, and later appends may
		 * actually replace one element with another so we need to
		 * scan all members to handle that.  But for parsing, this is
		 * a big win!
		 */
		scan = JSON_END_POINTER(container);
		assert(scan->type == JSON_KEY && scan->next == NULL);
		scan->next = more;
		JSON_END_POINTER(container) = more;
	} else {
		for (scan = container->first; strcmp(scan->text, more->text); scan = scan->next) {
			if (!scan->next) {
				/* adding a new name */
				scan->next = more;
				JSON_END_POINTER(container) = more;
				return;
			}
		}

		/* Replace the value of the member at "scan" */
		json_free(scan->first);
		scan->first = more->first;
		more->first = NULL;
		json_free(more);
	}
}

/* Add data to an object, array, or key.  Returns 1 normally, or 0 for
 * errors (after calling json_throw()).
 */
int json_parse_append(json_parse_t *state, json_t *container, json_t *more)
{
	switch (container->type) {
	  case JSON_KEY:
		if (more->type == JSON_KEY) {
			json_throw(state, "Attempt to add a key as a value of a key");
			return 0;
		}
		/* If the key already has a value, free it before storing
		 * the new value.
		 */
		if (container->first)
			free(container->first);
		container->first = more;
		return 1;

	  case JSON_ARRAY:
		jappendarray(container, more);
		return 1;

	  case JSON_OBJECT:
		if (more->type != JSON_KEY) {
			json_throw(state, "Attempt to add unkeyed data to an object");
			return 0;
		}
		jappendobject(state, container, more);
		return 1;

	  case JSON_BADTOKEN:
		json_throw(state, "json_parse_append(..., JSON_BADTOKEN)\n");
		return 0;

	  default:
		json_throw(state, "Attempt to append into a non-container");
		return 0;
	}

}

/* Parse a single token, and return a pointer to the next token */
const char *json_token(const char *str, json_token_t *token)
{
	char *endptr;

	/* Maybe output a chunk of the buffer */
	if (json_debug_flags.buffer)
		fprintf(stderr, "Buffer:%.72s\n", str);

	/* initialize the token */
	memset(token, 0, sizeof *token);

	/* Skip leading whitespace */
	while (*str == ' ' || *str == '\t' || *str == '\r')
		str++;

	/* Many tokens are single characters */
	token->start = str;
	token->len = 1;

	/* first character usually determines the token type */
	token->start = str;
	token->len = 1;
	switch (*str)
	{
	  case '{':	token->type = JSON_OBJECT;	break;
	  case '}':	token->type = JSON_ENDOBJECT;	break;
	  case '[':	token->type = JSON_ARRAY;	break;
	  case ']':	token->type = JSON_ENDARRAY;	break;
	  case '\n':	token->type = JSON_NEWLINE;	break;
	  case '"':
	  	token->type = JSON_STRING;
	  	token->start = ++str;
	  	for (token->len = 0; str[token->len] && str[token->len] != '"'; token->len++)
	  	{
	  		if (str[token->len] == '\\' && str[token->len + 1] >= ' ')
	  			token->len++;
	  	}
	  	if (!str[token->len])
	  		token->type = JSON_BADTOKEN;
	  	else
	  		str++;
	  	break;

	  default:
		/* Can it be parsed as a number? */
		(void)strtod(str, &endptr);
		if (endptr != str)
		{
			token->type = JSON_NUMBER;
			token->len = (endptr - str);
		}
		else if (isalpha(*str))
		{
			/* Probably a symbol.  Collect its chars... */
			for (; isalnum(str[token->len]); token->len++)
			{
			}

			/* The only legal symbols are true, false, and null. */
			if ((token->len == 4 && !strncmp(str, "true", 4))
			 || (token->len == 5 && !strncmp(str, "false", 5)))
			{
				token->type = JSON_BOOL;
			}
			else if (token->len == 4 && !strncmp(str, "null", 4))
			{
				token->type = JSON_NULL;
			}
			else
			{
				/* Anything else is a bad token.  If it is
				 * immediately followed by a " character then
				 * we've probably encountered corrupted data
				 * and should consume the " to help synchronize
				 * again, so we get better error messages.
				 */
				token->type = JSON_BADTOKEN;
				if (str[token->len] == '"')
					token->len++;
			}
		}
		else if (*str == '-')
		{
			token->type = JSON_BADTOKEN;
			token->len = 1;
		}
		else
		{
			token->type = JSON_BADTOKEN;
			token->len = 0;
		}
	}

	/* Skip trailing whitespace */
	for (str += token->len; *str == ' ' || *str == '\t' || *str == '\r'; str++)
	{
	}

	/* Strings may be used as keys.  Look for ":" */
	if (token->type == JSON_STRING && *str == ':')
	{
		token->type = JSON_KEY;
		str++;
		while (*str == ' ' || *str == '\t' || *str == '\r')
			str++;
	}

	/* Skip comma between elements */
	if (*str == ',')
	{
		str++;
		while (*str == ' ' || *str == '\t' || *str == '\r')
			str++;
	}

	/* Maybe output debugging info per token */
	if (json_debug_flags.token)
	{
		char *quote = "";
		char *typename;
		switch (token->type)
		{
		  case JSON_NEWLINE:	typename = "NEWLINE";	break;
		  case JSON_OBJECT:	typename = "OBJECT";	break;
		  case JSON_ENDOBJECT:	typename = "ENDOBJECT";	break;
		  case JSON_ARRAY:	typename = "ARRAY";	break;
		  case JSON_ENDARRAY:	typename = "ENDARRAY";	break;
		  case JSON_KEY:	typename = "KEY"; quote="\"";	break;
		  case JSON_STRING:	typename = "STRING";quote="\"";	break;
		  case JSON_NUMBER:	typename = "NUMBER";	break;
		  case JSON_BOOL:	typename = "BOOL";	break;
		  case JSON_NULL:	typename = "NULL";	break;
		  default:		typename = "BADTOKEN";
		}
		fprintf(stderr, "%s %s%.*s%s\n", typename, quote, (int)token->len, token->start, quote);
	}

	/* Return a pointer to the next token */
	return str;
}

/* Create a parsing context */
void json_parse_begin(json_parse_t *state, int maxnest)
{
	/* clobber it */
	memset(state, 0, sizeof(json_parse_t));

	/* allocate the stack */
	state->stack = (json_t **)calloc(sizeof(json_t *), maxnest);
	state->maxnest = maxnest;
	state->nest = -1;
}


/* Alert the parse state that you're starting a new line buffer.  You should
 * call this for each line, when reading from a file.
 */
void json_parse_newbuf(json_parse_t *state, const char *buf, long offset)
{
	state->lineno++;
	state->buf = buf;
	state->offset = offset;
	state->token = NULL;
}

/* Make a parsing context stop parsing */
void json_parse_stop(json_parse_t *state)
{
	/* Defend against a NULL state.  This is necessary because
	 * json_parse_stop() is sometimes called from an error catcher
	 * (see json_catch()), and the error catcher might be called
	 * when no state is available, such as when you try to add
	 * unkeyed data into an object.
	 */
	if (!state)
		return;

	/* Mark it as "stopped" */
	state->stop = 1;

	/* Data in keys isn't resolved until the data is done.  Well, its
	 * certainly done now, so we'd better deal with it.
	 */
	for (; state->nest > 0; state->nest--)
	{
		if ((state->stack[state->nest - 1]->type == JSON_KEY
		  || state->stack[state->nest - 1]->type == JSON_ARRAY
		  || state->stack[state->nest - 1]->type == JSON_OBJECT)
		 && state->stack[state->nest - 1]->first != state->stack[state->nest])
			json_parse_append(state, state->stack[state->nest - 1], state->stack[state->nest]);
	}
}

/* Incorporate a token into the parsed object */
void json_parse_token(json_parse_t *state, json_token_t *token)
{
	json_t *json;

	/* remember the position of this token, for debugging */
	if (token->type != JSON_BADTOKEN)
	{
		state->token = token->start;
		state->len = token->len;
	}

	/* process the token */
	switch (token->type)
	{
	  case JSON_BADTOKEN:
		/* Ignore a non-token at the end of a line.  This can happen
		 * when blank lines appear in the JSON text, which is legal.
		 */
		if (*token->start == '\0')
			return;

		json_throw(state, "Bad token \"%.*s\"", token->len, token->start);
		return;

	  case JSON_NEWLINE:
		/* Newlines aren't expected in NUL-terminated strings, so if we
		 * see this, our buffer must be continuous.  For debugging
		 * purposes, it helps to treat each line as a separate buffer.
		 */
		json_parse_newbuf(state, token->start + 1, state->offset + token->start + 1  - state->buf);
		break;

	  case JSON_OBJECT:
	  	/* Merge an empty object into the JSON data, and also add it
	  	 * to the stack so any subsequent values will go into the new
	  	 * object.
	  	 */
	  	json = json_object();
	  	state->stack[++state->nest] = json;
	  	break;

	  case JSON_ENDOBJECT:
	  	/* Can't end an object if we weren't in an object */
	  	if (state->nest < 0 || state->stack[state->nest]->type != JSON_OBJECT)
	  	{
	  		json_throw(state, "Bad nesting at %.*s", token->len, token->start);
 			return;
	  	}

	  	/* Pop the object off the stack, so any following data will
	  	 * go into the parent.
	  	 */
	  	json = state->stack[state->nest--];

		/* Merge it into the outer object */
		if (json == JSON_STOP)
			json_parse_stop(state);
		else if (json && state->nest >= 0)
		{
			json_parse_append(state, state->stack[state->nest], json);
		}
	  	break;

	  case JSON_ARRAY:
	  	/* Merge an empty array into the JSON data, and also add it
	  	 * to the stack so any subsequent values will go into the new
	  	 * array.
	  	 */
	  	json = json_array();
		if (state->nest >= 0)
			json_parse_append(state, state->stack[state->nest], json);
	  	state->stack[++state->nest] = json;
	  	break;

	  case JSON_ENDARRAY:
	  	/* Pop the array off the stack, so any following data will
	  	 * go into the parent.
	  	 */
	  	if (state->nest < 0 || state->stack[state->nest]->type != JSON_ARRAY)
	  	{
	  		json_throw(state, "Bad nesting at %.*s", token->len, token->start);
	  		return;
	  	}
	  	state->nest--;
	  	break;

	  case JSON_KEY:
		/* You can't key a key */
		if (state->nest >= 0 && state->stack[state->nest]->type == JSON_KEY)
		{
			json = state->stack[state->nest];
			json_throw(state, "Attempt to nest keys \"%s\" and \"%.*s\"",
				json->text, token->len, token->start);
			return;
		}

		/* Push a key onto the stack, but don't append it yet.
		 * We'll wait until we have a value to do the merging
		 */
		state->stack[++state->nest] = json_simple(token->start, token->len, JSON_KEY);
		break;

	  case JSON_STRING:
	  case JSON_NUMBER:
	  case JSON_BOOL:
	  case JSON_NULL:
		/* Merge the value into the object/array/key on the top of
		 * the stack.
		 */
		json = json_simple_from_token(token);
		if (state->nest < 0)
			state->stack[0] = json;
		else
		{
			json_parse_append(state, state->stack[state->nest], json);
		}
		break;

	  case JSON_DEFERRED:
		/* Internal use only -- never parsed */
		;
	}

	/* If we just appended something into a key, then we need to pop
	 * that key off the stack and append it with the object/array it's
	 * nested in.
	 */
	if (state->nest >= 0 && state->stack[state->nest]->type == JSON_KEY)
	{
		switch (token->type)
		{
		  case JSON_ENDOBJECT:
		  case JSON_ENDARRAY:
		  case JSON_STRING:
		  case JSON_NUMBER:
		  case JSON_BOOL:
		  case JSON_NULL:
			state->nest--;
			if (state->nest >= 0)
			{
				json_parse_append(state, state->stack[state->nest], state->stack[state->nest + 1]);
			}
			break;

		  default:
		  	;/* no action needed */
		}
	}
}

/* Determine whether a parse is complete.  This is valid after at least
 * one token has been parsed, but before json_parse_end() has been called.
 */
int json_parse_complete(json_parse_t *state)
{
	/* If the stack is empty, it is complete */
	if (state->nest < 0 || state->stop)
		return 1;
	return 0;
}

/* Free the parsing context's resources, and return the parsed object */
json_t *json_parse_end(json_parse_t *state)
{
	json_t *json;

	/* Complain if we aren't at a good stopping point */
	if (!json_parse_complete(state))
	{
		json_throw(state, "Incomplete parse");
		return NULL;
	}

	/* Free the stack (but remember the json_t tree first) */
	json = state->stack[0];
	free(state->stack);

	return json;
}

/* Parse a string containing a complete JSON object and return it */
json_t *json_parse_string(const char *str)
{
	json_parse_t	state;
	json_token_t	token;

	/* Start parsing */
	json_parse_begin(&state, 30);
	json_parse_newbuf(&state, str, 0L);

	/* Parse each token and add it to data */
	while (*str && !state.stop)
	{
		str = json_token(str, &token);
		json_parse_token(&state, &token);
	}

	/* end parsing */
	return json_parse_end(&state);
}

/* Parse a file, or stdin if "-" is given for filename.
 *
 * If state is NULL then an internal state will be used, and this function
 * returns the json_t tree.  If you do pass a state, then this function
 * returns the json_t tree for the data parsed to far, but you need to call
 * json_parse_end() to get the final value.
 */
json_t *json_parse_file(json_parse_t *state, char *filename)
{
	json_parse_t	internalstate;
	json_token_t	token;
	FILE		*fp;
	char		*buf;
	const char	*cursor, *nextcursor;
	size_t		bufsize, got, more;
	long		lineno;

	/* If passed a parse state, and it is stopped, then do nothing */
	if (state && state->stop)
		return state->stack[0];

	/* Open the file */
	if (!strcmp(filename, "-"))
		fp = stdin;
	else
	{
		fp = fopen(filename, "r");
		if (!fp)
		{
			perror(filename);
			return NULL;
		}
	}

	/* Start parsing */
	if (!state)
	{
		state = &internalstate;
		json_parse_begin(state, 30);
	}
	state->file = filename;

	/* Allocate & fill the initial buffer */
	bufsize = 100;
	buf = (char *)malloc(bufsize);
	json_parse_newbuf(state, buf, 0L);
	got = fread(buf, 1, bufsize - 1, fp);
	if (json_debug_flags.file)
		fprintf(stderr, "Read the first %d bytes of %s\n", (int)got, state->file);
	cursor = buf;
	buf[got] = '\0';

	while (!state->stop)
	{
		/* Try to parse a token.  If it hits the end of the buffer
		 * then either shift the buffer or expand it, and try again.
		 * Note that we prevent json_parse_newbuf() from messing with
		 * the line number, because we aren't doing this one line at
		 * a time.
		 */
		lineno = state->lineno;
		nextcursor = json_token(cursor, &token);
		while (nextcursor == &buf[got] && !feof(fp))
		{
			/* We hit the end of the buffer.  Can we shift? */
			if (cursor != buf)
			{
				if (json_debug_flags.file)
					fprintf(stderr, "Shifting buffer by %d bytes\n", (int)(cursor - buf));
				memmove(buf, cursor, bufsize - (cursor - buf));
				got -= (cursor - buf);
				state->offset += (cursor - buf);
			}
			else /* No room to shift, this is just a big token! */
			{
				bufsize *= 2;
				if (json_debug_flags.file)
					fprintf(stderr, "Enlarging buffer to %d bytes\n", (int)bufsize);
				buf = (char *)realloc(buf, bufsize * 2);
				json_parse_newbuf(state, buf, state->offset);
			}

			/* One way or another, there is now more room in the
			 * buffer, and this token starts at the front of it.
			 * Fill the rest of the buffer with text from the file.
			 */
			cursor = buf;
			more = fread(buf + got, 1, bufsize - got - 1, fp);
			if (json_debug_flags.file)
				fprintf(stderr, "Read %d more bytes from %s\n", (int)more, state->file);
			got += more;
			buf[got] = '\0';

			nextcursor = json_token(cursor, &token);
		}
		state->lineno = lineno;
		cursor = nextcursor;

		/* Expect a JSON_BADTOKEN at the end of the file */
		if (feof(fp) && token.type == JSON_BADTOKEN)
			break;

		/* Parse it */
		json_parse_token(state, &token);
	}

	/* Close the file */
	if (fp != stdin)
		fclose(fp);

	/* Free the input buffer */
	free(buf);
	if (json_debug_flags.file)
		fprintf(stderr, "Done reading %s\n", state->file);

	/* End parsing */
	if (state == &internalstate)
		return json_parse_end(state);
	else
		return state->stack[0];
}
