#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"

/*****************************************************************************/
/* The following code supports name completion in the readline() function.   */

/* This returns strdup(str), unless str contains characters that might confuse
 * the parser such as spaces or punctuation; in those cases, it adds quotes.
 */
static void maybeQuoteCpy(char *dst, char *str)
{
	/* Decide whether quoting is needed */
	int	mustQuote = 0;
	char	*tmp;
	size_t	len;

	if (!isalpha(*str) && *str != '_')
		mustQuote = 1;
	else for (tmp = str + 1; *tmp; tmp++) {
		if (!isalnum(*tmp) && *tmp != '_')
			mustQuote = 1;
	}

	if (mustQuote)
		*dst++ = '`';
	strcpy(dst, str);
	if (mustQuote)
		strcat(dst, "`");
}

/* This tries to complete a global name by scanning the context. */
static char *global_name_generator(const char *text, int state)
{
	static size_t len;
	static json_t *scan;
	static jsoncontext_t *con;
	char	*tmp;

	/* First time? */
	if (state == 0) {
		/* Start with the newest context */
		con = context;
		scan = NULL;
		len = json_mbs_len(text);
	}

	/* If scan is NULL, then move it to the start of current context
	 * except that if the context's data isn't an object then skip it.
	 * If scan is non-NULL then just try moving to the next.
	 */
	if (!scan) {
		/* Skip non-object contexts */
		while (con && con->data->type != JSON_OBJECT)
			con = con->older;
		if (!con)
			return NULL;
		scan = con->data->first;
	} else {
		scan = scan->next;
	}

	/* Now loop, looking for the next partial match.  If found, return it.
	 * If not found and we reach the end, then return NULL.
	 */
	for (;;) {
		for (; scan; scan = scan->next) {
			if (!json_mbs_ncasecmp(scan->text, text, len)) {
				if (scan->first->type == JSON_OBJECT)
					rl_completion_append_character = '.';
				else
					rl_completion_suppress_append = 1;
				tmp = (char *)malloc(strlen(scan->text) + 3);
				maybeQuoteCpy(tmp, scan->text);
				return tmp;
			}
		}
		do {
			con = con->older;
		} while (con && con->data->type != JSON_OBJECT);
		if (!con)
			return NULL;
		scan = con->data->first;
	}
}

/* For member completions, completion_container is set by jsoncalc_completion()
 * to an object whose member names are to be scanned.  completion_key is the
 * partial key to look for -- a pointer into readline's line buffer.  This
 * can be combined with the "text" parameter to determine the whole string
 * to return.
 */
static json_t	*completion_container;
static int	completion_key_offset;

/* Return the first (if state=0) or next (if state>0) name that matches text.
 * If there are no more matches, return NULL.  The returned value should be
 * dynamically allocated, e.g. via strdup(); readline() will free it.
 */
char *member_name_generator(const char *text, int state)
{
	static json_t *scan;
	char	buf[1000];
	size_t	len;

	/* Make a mutable copy of text */
	strcpy(buf, text);

	/* If first, then reset scan */
	if (!state)
		scan = completion_container->first;
	else
		scan = scan->next;

	/* We're looking in a container (object) */
	len = json_mbs_len(text + completion_key_offset);
	for (; scan; scan = scan->next) {
		if (!json_mbs_ncasecmp(scan->text, text + completion_key_offset, len)) {
			if (scan->first->type == JSON_OBJECT)
				rl_completion_append_character = '.';
			else if (scan->first->type == JSON_ARRAY)
				rl_completion_append_character = '[';
			else
				rl_completion_suppress_append = 1;
			maybeQuoteCpy(&buf[completion_key_offset], scan->text);
			return strdup(buf);
		}
	}

	return NULL;
}

/* This collects all matching names into a dynamically allocated array of
 * dynamically allocated strings, and returns the array.  "text" is a pointer
 * to the start of the text within readline()'s input buffer.  "start" is
 * the number of additional characters available before "text".  "end"
 * is the index of the end of "text" though "text" is NUL-terminated so it
 * isn't all than necessary.
 */
char **jsoncalc_completion(const char *text, int start, int end)
{
	char	*key, *next, c;
	int	scan, state;
	char	buf[1000];

	/* Make a non-const copy of the whole line (not just "text") */
	strcpy(buf, rl_line_buffer);
	buf[end] = '\0';

	/* We either get "na" or "name.na" or ".name.na".  The first completes
	 * by looking for names in the context.  The second starts by fetching
	 * the value of a global name from the context, and then steps down
	 * through the expression until the end, where we have a container
	 * (object, hopefully) and a partial name to look for in it.  The third
	 * form is preceded by something too complex for readline() to handle
	 * for us, but if it's a subscript then it might be preceded by other
	 * names, starting with a global name.
	 */

	/* Let's start with the easy case: global name completion */
	if (!strchr(text, '.')) {
		/* No dots, must be partial global */
		return rl_completion_matches(text, global_name_generator);
	}

	/* If the given text starts with "." then check to see if it's preceded
	 * by a subscript and names.
	 */
	if (*text == '.') {
		/* If not preceded by a subscript and names, we can't help */
		if (start < 3 || buf[start - 1] != ']') {
			return NULL;
		}

		/* Skip back over any subscripts (even if complex) and any
		 * names/dots outside of subscripts.
		 */
		for (scan = start - 1, state = 0; state >= 0; scan--) {
			if (scan == 0) {
				if (state > 0)
					return NULL;
				break;
			}
			if (buf[scan] == '[')
				state--;
			else if (buf[scan] == ']')
				state++;
			else if (state == 0 && strchr(rl_basic_word_break_characters, buf[scan - 1]) != NULL)
				break;
		}

		/* At this point, buf[scan] is either the start of a global
		 * name, or it is ".".  If "." we can't do anything with it.
		 */
		if (buf[scan] == '.')
			return NULL;
	} else {
		scan = start;
	}

	/* At this point, one way or another, buf[scan] is the start of a
	 * whole global name.  Look it up.
	 */
	next = strpbrk(&buf[scan], ".[");
	c = *next;
	*next = '\0';
	completion_container = json_context_by_key(context, &buf[scan], NULL);
	*next = c;
	if (!completion_container)
		return NULL;

	/* Step through names or subscripts, until we get back to partial */
	for (;;) {
		/* skip '.' or subscript */
		if (c == '.')
			next++;
		else {
			for (state = 1, next++; state > 0; next++) {
				if (*next == '[')
					state++;
				else if (*next == ']')
					state--;
			}
			if (*next == '.') /* and it always should be */
				next++;
		}
		key = next;

		/* Find the end of this word.  If this is the last word,
		 * then do the completion thing.
		 */
		next = strpbrk(key, ".[");
		if (!next) {
			completion_key_offset = key - buf - start;
			return rl_completion_matches(text, member_name_generator);
		}
		c = *next;
		*next = '\0';

		/* Find this container */
		completion_container = json_by_key(completion_container, key);
		*next = c;

		/* Since we're skipping over subscripts, if this is an array
		 * then skip into its first element.  If no elements, then
		 * we can't do the completion.
		 */
		while (completion_container && completion_container->type == JSON_ARRAY)
			completion_container = completion_container->first;
		if (!completion_container)
			return NULL;
	}
}
