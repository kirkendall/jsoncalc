#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"

/*****************************************************************************/
/* The following code supports name completion in the readline() function.   */


/* Copy a string, adding quotes if necessary. */
static void maybeQuoteCpy(char *dst, char *str)
{
	/* Decide whether quoting is needed */
	int	mustQuote = 0;
	char	*tmp;

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

/* This tries to complete the name of a config setting.  A name could be...
 *   - a key in json_config
 *   - a key in json_config.interactive
 *   - the prefix "no" on either of those keys, if boolean
 *   - an element in a "*-list" array in either of those places
 * Note that the latter could be something like "on green" but since "green"
 * is also an possibility, we don't need to be smart about that.
 */
static json_t *config_section;
static char *config_name_generator(const char *text, int state)
{
	static size_t len;
	static json_t *section, *scan, *elem;
	char	*tmp;

	/* First time? */
	if (state == 0) {
		/* Basic initialization */
		section = config_section; /* usually json_config */
		scan = section->first;
		elem = NULL;
		len = json_mbs_len(text);

		/* If there's already a name=... then look for that exact name.
		 * If its value is a section, then use that section instead of
		 * json_config or json_config.interactive.
		 */

	}

	/* If scan is NULL and section is json_config, then move to the start
	 * of json_config.interactive.
	 */
	if (!scan) {
		if (section != json_config)
			return NULL;
		section = json_by_key(json_config, "interactive");
		scan = section->first;
	}

	/* Loop until we find EITHER a matching partial name, or a "*-list"
	 * array.  For the array, scan for matching elements.
	 */
	while (scan) {
		/* Is this an "-list" array? */
		if (scan->first->type == JSON_ARRAY && json_mbs_like(scan->text, "%-list")) {
			/* Scan the elements.  If not resuming an earlier scan
			 * then start at the array's first element.
			 */
			if (!elem)
				elem = scan->first->first;
			for (; elem; elem = elem->next) {
				if (elem->type == JSON_STRING && !json_mbs_ncmp(text, elem->text, len)) {
					/* found a matching list element */
					rl_completion_suppress_append = 1;
					tmp = (char *)malloc(strlen(elem->text) + 3);
					maybeQuoteCpy(tmp, elem->text);
					elem = elem->next;

					return tmp;
				}
			}

		} else if (!json_mbs_ncmp(text, scan->text, len)) {
			/* found a matching member name */
			rl_completion_suppress_append = 1;
			tmp = (char *)malloc(strlen(scan->text) + 3);
			maybeQuoteCpy(tmp, scan->text);
			scan = scan->next;
			return tmp;
		} else if (text[0] == 'n'
			&& text[1] == 'o'
			&& scan->first->type == JSON_BOOL
			&& (len == 2 || !json_mbs_ncmp(text + 2, scan->text, len - 2))) {
			/* Found a matching member name for a "no" boolean */
			rl_completion_suppress_append = 1;
			tmp = (char *)malloc(strlen(scan->text) + 3);
			tmp[0] = 'n';
			tmp[1] = 'o';
			strcpy(tmp + 2, scan->text);
			scan = scan->next;
			return tmp;
		}

		/* No match.  Move to the next member. */
		scan = scan->next;
		if (!scan && section == json_config) {
			section = json_by_key(json_config, "interactive");
			scan = section->first;
		}
	}

	return NULL;
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

/* This function is called from GNU readline.  It collects all matching names
 * into a dynamically allocated array of dynamically allocated strings, and
 * returns the array.  "cursor" is a pointer into the text where the next
 * completion is needed.  "start" is the number of bytes before "cursor",
 * and "end" is the number of additional characters after "cursor" (though
 * the line buffer is '\0'-terminated so "end" isn't really needed.)
 */
char **jsoncalc_completion(const char *text, int start, int end)
{
	char	*key, *next, c;
	int	scan, state;
	char	buf[1000];

	/* Make a non-const copy of the whole line (not just "text") */
	strcpy(buf, rl_line_buffer);
	buf[start + end] = '\0';

	/* As a special case, "set ..." does completions from the config
	 * data.
	 */
	if (start >= 4 && !strncmp(buf, "set ", 4)) {
		/* First, though, check to see if we've already hit a "name="
		 * where the name's value is an object, in which case use that
		 * as the section instead of all of json_config.
		 */
		int equal = 0;
		config_section = json_config;
		for (scan = start; scan >= 4 && buf[scan] != ','; scan--) {
			if (buf[scan] == '=')
				equal = scan;
		}
		for (; scan < start && buf[scan] == ' '; scan++) {
		}
		if (scan < start && scan < equal) {
			buf[equal] = '\0';
			config_section = json_by_key(json_config, &buf[scan]);
			if (!config_section)
				config_section = json_by_key(json_by_key(json_config, "interactive"), &buf[scan]);
			if (!config_section)
				config_section = json_config;
			buf[equal] = '=';
		}

		return rl_completion_matches(text, config_name_generator);
	}

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

	/* If the given text starts with "." then check to see if it's
	 * preceded by a subscript and names.
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
