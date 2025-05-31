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
	"grid",	/* table - output tables in grid format (sh/grid/json) */
	0,	/* string - output strings as plain text */
	1,	/* pretty - pretty-print (add whitespace to show structure) */
	0,	/* elem - force one array element per line */
	0,	/* sh - quote output for shell */
	1,	/* error - write error nulls to stderr */
	0,	/* ascii - use \uXXXX for non-ASCII characters */
	1,	/* color - use colors on ANSI terminals */
	0,	/* quick - for csv/grid, use first record to find columns */
	1,	/* graphic - use Unicode graphic characters */
	"",	/* prefix - prepended to names for sh format */
	"",	/* null - text to show as null for grid format */
	"\033[36m",	/* escresult - coloring for result */
	"\033[4;32m",	/* eschead - coloring for last grid heading line */
	"\033[32m",	/* eschead2 - coloring for other grid heading lines */
	"\033[32m",	/* escdelim - coloring for table column delimiter */
	"\033[31m",	/* escerror - coloring for errors */
	"\033[33m",	/* escdebug - coloring for debugging output */
	NULL	/* output - NULL for stdout, else the fp to write to */
};

/* This is the escape sequence to end coloring */
char json_format_color_end[20] = "\033[m";


/* Convert a color name or other attribute to a number */
static char *colorcode(char *esc, const char *color)
{
	int	i;
	char	*code;

	/* Map names to numbers, for coloring escape sequences */
	static struct {
		char *name;
		char code[3];
	} colors[] = {
		{"bold","1"},{"dim","2"},{"italic","3"},{"underlined","4"},
		{"blinking","5"},{"boxed","7"},
		{"black","30"},{"red","31"},{"green","32"},{"yellow","33"},
		{"blue","34"},{"magenta","35"},{"cyan","36"},{"white","37"},
		{"on black","40"},{"on red","41"},{"on green","42"},
		{"on yellow","43"},{"on blue","44"},{"on magenta","45"},
		{"on cyan","46"},{"on white","47"},
		{NULL}
	};

	/* Defend against NULL */
	if (!color)
		return esc;

	/* Scan for the code, by name */
	for (i = 0; colors[i].name; i++)
		if (!strcmp(colors[i].name, color)) {
			/* Copy it, with a ; appended */
			for (code = colors[i].code; *code; )
				*esc++ = *code++;
			*esc++ = ';';
			return esc;
		}

	return esc;
}

/* Convert a color from the config object to an escape sequence */
static void formatcolor(char *esc, json_t *config, const char *name, int nounderlined)
{
	json_t	*color;
	char	*wholeesc = esc;

	/* Find the nested object describing this color */
	color = json_by_key(config, name);
	if (!color) {
		*esc = '\0';
		return;
	}

	/* Start the escape sequence */
	*esc++ = '\033';
	*esc++ = '[';

	/* Foreground and background */
	esc = colorcode(esc, json_text_by_key(color, "fg"));
	esc = colorcode(esc, json_text_by_key(color, "bg"));

	/* Other attributes */
	if (json_is_true(json_by_key(color, "bold")))
		esc = colorcode(esc, "bold");
	if (json_is_true(json_by_key(color, "dim")))
		esc = colorcode(esc, "dim");
	if (json_is_true(json_by_key(color, "italic")))
		esc = colorcode(esc, "italic");
	if (!nounderlined && json_is_true(json_by_key(color, "underlined")))
		esc = colorcode(esc, "underlined");
	if (json_is_true(json_by_key(color, "blinking")))
		esc = colorcode(esc, "blinking");
	if (json_is_true(json_by_key(color, "boxed")))
		esc = colorcode(esc, "boxed");

	/* We should have an extra ";" at the end, which we'll convert to
	 * 'm' to complete the sequence.  If there is no ';' then there are
	 * no attributes set so we might as well skip the whole sequence.
	 */
	if (esc[-1] == ';') {
		esc[-1] = 'm';
		esc[0] = '\0';
	} else
		*wholeesc = '\0';
}


/* Initialize the format and colors from json_config or some other config.
 * "format" is the format to be modified; if NULL then json_format_default
 * is used.  "config" is the json_t object describing the configuration,
 * will usually be NULL to indicate json_config should be used.
 */
void json_format_set(jsonformat_t *format, json_t *config)
{
	json_t	*section; /* Format section to use - interactive or batch */

	/* If no config was specified (i.e., NULL) then use json_config */
	if (!config)
		config = json_config;

	/* If no format was specified, use the json_format_default */
	if (!format)
		format = &json_format_default;

	/* Choose a section - "interactive" or "batch" */
	section = json_by_key(config, json_text_by_key(json_system, "runmode"));

	/* Copy fields from config object to the format */
	format->tab = json_int(json_by_key(section, "tab"));
	format->oneline = json_int(json_by_key(section, "oneline"));
	format->digits = json_int(json_by_key(section, "digits"));
	strncpy(format->table, json_text_by_key(section, "table"), sizeof format->table - 1);;
	format->string = json_is_true(json_by_key(section, "string"));
	format->pretty = json_is_true(json_by_key(section, "pretty"));
	format->elem = json_is_true(json_by_key(section, "elem"));
	format->sh = json_is_true(json_by_key(section, "sh"));
	format->errors = json_is_true(json_by_key(section, "errors"));
	format->ascii = json_is_true(json_by_key(section, "ascii"));
	format->color = json_is_true(json_by_key(section, "color"));
	format->quick = json_is_true(json_by_key(section, "quick"));
	format->graphic = json_is_true(json_by_key(section, "graphic"));
	strncpy(format->prefix, json_text_by_key(section, "prefix"), sizeof format->prefix - 1);
	format->prefix[sizeof format->prefix - 1] = '\0';
	strncpy(format->null, json_text_by_key(section, "null"), sizeof format->null - 1);
	format->null[sizeof format->null - 1] = '\0';
	formatcolor(format->escresult, config, "result", 0);
	formatcolor(format->escgridhead, config, "gridhead", 0);
	formatcolor(format->escgridhead2, config, "gridhead", 1);
	formatcolor(format->escgridline, config, "gridline", 0);
	formatcolor(format->escerror, config, "error", 0);
	formatcolor(format->escdebug, config, "debug", 0);
}
