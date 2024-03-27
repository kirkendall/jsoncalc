#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "json.h"

/* This is the default format.  It is used if you pass NULL to functions
 * that use jsonformat_t, especially json_print().
 */
jsonformat_t json_format_default = {
	2,	/* tab - indentation to use when pretty-printing */
	50,	/* oneline - JSON shorter than this will always be compact */
	12,	/* digits - precission when converting from double to text */
	0,	/* elem - force one array element per line */
	0,	/* csv - output tables in CSV format */
	0,	/* sh - output tables in SH format */
	1,	/* grid - output tables in grid format */
	0,	/* string - output strings as plain text */
	1,	/* pretty - pretty-print (add whitespace to show structure) */
	1,	/* color - use colors on ANSI terminals */
	0,	/* ascii - use \uXXXX for non-ASCII characters */
	0,	/* quick - for csv/grid, use first record to find columns */
	"",	/* prefix - prepended to names for sh format */
	""	/* null - text to show as null for grid format */
};

/* These store escape sequences for coloring various parts of output */
char json_format_color_result[20] = "\033[36m";
char json_format_color_head[20] = "\033[4;32m";
char json_format_color_delim[20] = "\033[32m";
char json_format_color_error[20] = "\033[31m";
char json_format_color_debug[20] = "\033[33m";
char json_format_color_end[20] = "\033[m";

/* Parse a formatting string (e.g., the argument of an -O flag).  Return NULL
 * on success, or an error message on error.
 */
char *json_format(jsonformat_t *format, char *str)
{
	char	*end, *value, v;
	size_t	namelen;

	/* If no format buffer is specified, use the default */
	if (!format)
		format = &json_format_default;

	/* str is a comma-delimited list of specifiers.  These can be either
	 * name=value, or just name to set it to Y or some other default.
	 */
	while (*str) {
		/* Find the end */
		end = strchr(str, ',');
		if (!end)
			end = str + strlen(str);

		/* Find the length of the name */
		for (namelen = 0; isalpha(str[namelen]); namelen++) {
		}

		/* Does it have a value? */
		if (str[namelen] == '=')
			value = str + namelen + 1;
		else
			value = NULL;

		/* process the name */
		if (namelen == 3 && !strncmp("tab", str, namelen))
			format->tab = value ? atoi(value) : 2;
		else if (namelen == 5 && !strncmp("oneline", str, namelen))
			format->oneline = value ? atoi(value) : 0;
		else if (namelen == 5 && !strncmp("digits", str, namelen))
			format->digits = value ? atoi(value) : 0;
		else if (namelen == 6 && !strncmp("prefix", str, namelen))
			strncpy(format->prefix, value ? value : "", sizeof format->prefix  - 1);
		else if (namelen == 4 && !strncmp("null", str, namelen))
			strncpy(format->null, value ? value : "", sizeof format->null  - 1);
		else {
			/* Convert value to a true/false indicator */
			if (value)
				v = strchr("NnFf0", *value) ? 0 : 1;
			else if (!strncmp(str, "no", 2)) {
				v = 0;
				str += 2;
				namelen -= 2;
			} else
				v = 1;

			/* Store it as appropriate for the name. */
			if (namelen == 4 && !strncmp("elem", str, namelen))
				format->elem = v;
			else if (namelen == 3 && !strncmp("csv", str, namelen))
				format->csv = v, format->sh = format->grid = 0;
			else if (namelen == 2 && !strncmp("sh", str, namelen))
				format->sh = v, format->csv = format->grid = 0;
			else if (namelen == 4 && !strncmp("grid", str, namelen))
				format->grid = v, format->csv = format->sh = 0;
			else if (namelen == 6 && !strncmp("string", str, namelen))
				format->string = v;
			else if (namelen == 6 && !strncmp("pretty", str, namelen))
				format->pretty = v;
			else if (namelen == 5 && !strncmp("color", str, namelen))
				format->color = v;
			else if (namelen == 5 && !strncmp("ascii", str, namelen))
				format->ascii = v;
			else if (namelen == 5 && !strncmp("quick", str, namelen))
				format->quick = v;
			else
				return "Invalid name in format string";
		}

		/* Prep for next.  Skip comma and whitespace*/
		str = end;
		while (*str == ',' || isspace(*str)) {
			str++;
		}
	}

	/* Done! */
	return NULL;
}

/* Convert a jsonformat_t back to a string.  Passing NULL as the jsonformat_t
 * will make it use the json_format_default settings.  The string is dynamically
 * allocated, so the calling function will need to free it via free().
 */
char *json_format_str(jsonformat_t *fmt)
{
	char	buf[200];

	/* Use json_format_default by default */
	if (!fmt)
		fmt = &json_format_default;

	/* Start the string, most important stuff first */
	if (fmt->pretty)
		strcpy(buf, "pretty,");
	else if (fmt->elem)
		strcpy(buf, "elem,");
	else if (fmt->oneline > 0)
		sprintf(buf, "oneline=%d,", fmt->oneline);

	/* Don't bother with tab if we're compact */
	if (fmt->pretty || fmt->elem)
		sprintf(buf + strlen(buf), "tab=%d,", fmt->tab);

	/* If digits isn't the default of 12, then output it */
	if (fmt->digits != 12)
		sprintf(buf + strlen(buf), "digits=%d,", fmt->digits);

	/* String format */
	if (fmt->sh)
		strcat(buf, "string,");

	/* Table formats */
	if (fmt->sh)
		strcat(buf, "sh,");
	else if (fmt->csv)
		strcat(buf, "csv,");
	else if (fmt->grid)
		strcat(buf, "grid,");

	/* If "sh" then output prefix */
	if (fmt->sh && *fmt->prefix)
		sprintf(buf + strlen(buf), "prefix=%s,", fmt->prefix);

	/* If "table" then output the null string */
	if (fmt->grid && *fmt->null)
		sprintf(buf + strlen(buf), "null=%s,", fmt->null);

	/* Always add the last few booleans, and the "digits" setting */
	sprintf(buf + strlen(buf), "%s%s%s",
		fmt->quick ? "quick," : "",
		fmt->ascii ? "ascii," : "noascii,",
		fmt->color ? "color" : "nocolor");

	/* Return a dynamically-allocated copy of that */
	return strdup(buf);
}

static struct {
	char *name;
	int  code;
} colors[] = {
	{"bold", 1}, {"underlined", 4},
	{"black", 30}, {"red", 31}, {"green", 32}, {"yellow", 33},
	{"blue", 34}, {"magenta", 35}, {"cyan", 36}, {"white", 37},
	{"on black", 40}, {"on red", 41}, {"on green", 42}, {"on yellow", 43},
	{"on blue", 44}, {"on magenta", 45}, {"on cyan", 46}, {"on white", 47},
	{NULL}
};

/* Parse a color string.  This is a comma-delimited list of "name=value"
 * pairs, where the name is a color role (one of result, head, delim, error,
 * or debug) and the value is a color such as "blue on white" or "bold red".
 * Returns NULL normally, or an error message if there's trouble.
 */
char *json_format_color(char *str)
{
	int	len, i;
	char	*esc;
	for (esc = NULL; *str; str++) {
		if (!isalpha(*str))
			continue;
		for (len = 1; isalpha(str[len]); len++) {
		}
		if (len == 2 && !strncasecmp(str, "on ", 3)) {
			for (len += 2; isalpha(str[len]); len++) {
			}
		}
		if (!strncasecmp("result", str, len)) {
			if (esc && *esc) strcat(esc, "m");
			*(esc = json_format_color_result) = '\0';
		} else if (!strncasecmp("head", str, len)) {
			if (esc && *esc) strcat(esc, "m");
			*(esc = json_format_color_head) = '\0';
		} else if (!strncasecmp("delim", str, len)) {
			if (esc && *esc) strcat(esc, "m");
			*(esc = json_format_color_delim) = '\0';
		} else if (!strncasecmp("error", str, len)) {
			if (esc && *esc) strcat(esc, "m");
			*(esc = json_format_color_error) = '\0';
		} else if (!strncasecmp("debug", str, len)) {
			if (esc && *esc) strcat(esc, "m");
			*(esc = json_format_color_debug) = '\0';
		} else {
			for (i = 0;
			     colors[i].name && strncasecmp(colors[i].name, str, len);
			     i++) {
			}
			if (!colors[i].name)
				return "Unrecognized word in color string";
			if (!esc)
				return "Missing color role name in color string";
			if (!*esc)
				strcpy(esc, "\033[");
			else
				strcat(esc, ";");
			sprintf(esc + strlen(esc), "%d", colors[i].code);
		}
		str += len - 1;
	}
	if (esc && *esc) strcat(esc, "m");

	return NULL;
}


/* Generate a descriptive string from an escape sequence.  Return a pointer to
 * the end of the buffer.
 */
static char *esc_to_text(char *buf, char *name, char *esc)
{
	char *scan;
	int	i, any;

	*buf = '\0';
	if (name) {
		strcpy(buf, name);
		strcat(buf, "=");
	}
	any = 0;
	for (scan = esc; *scan; scan++) {
		if ((scan == esc || !isdigit(scan[-1])) && isdigit(*scan)) {
			int code = atoi(scan);
			for (i = 0; colors[i].name && colors[i].code != code; i++) {
			}
			if (colors[i].name) {
				if (any)
					strcat(buf, " ");
				strcat(buf, colors[i].name);
				any = 1;
			}
		}
	}
	if (!any)
		strcat(buf, "normal");
	return buf + strlen(buf);
}

/* Return a string describing the current colors */
char *json_format_color_str()
{
	char buf[100], *build;

	memset(buf, 0, sizeof buf);
	build = buf;
	build = esc_to_text(build, "result", json_format_color_result);
	*build++ = ',';
	build = esc_to_text(build, "head", json_format_color_head);
	*build++ = ',';
	build = esc_to_text(build, "delim", json_format_color_delim);
	*build++ = ',';
	build = esc_to_text(build, "error", json_format_color_error);
	*build++ = ',';
	build = esc_to_text(build, "debug", json_format_color_debug);

	return strdup(buf);
}
