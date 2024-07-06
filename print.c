#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "json.h"

/* Print a json_t tree as JSON text.  "format" controls the format.  */
static void jcprint(json_t *json, int indent, jsonformat_t *format)
{
	json_t	*scan;
	char	*str;

	/* output the indent */
	if (indent > 0 && (format->pretty || format->elem))
		fprintf(format->fp, "%*s", indent, "");

	/* If this is a key, then output the key and switch to its value */
	scan = json;
	if (json->type == JSON_KEY)
	{
                putc('"', format->fp);
                for (str = json->text; *str; str++) {
			if (*str == '"' || *str == '\\') {
				putc('\\', format->fp);
			}
			else if (*str == '\'' && format->sh) {
				/* For "sh" format, the entire output is
				 * enclosed in ' quotes which is great for
				 * everything except the ' character itself.
				 * For that, we need to end the quote, add a
				 * backslash-', and start a new quote.
				 */
				putc('\'', format->fp);
				putc('\\', format->fp);
				putc('\'', format->fp);
				putc('\'', format->fp);
			}
			putc(*str, format->fp);
		}
                putc('"', format->fp);
                putc(':', format->fp);
		scan = json->first;
	}

	switch (scan->type)
	{
	  case JSON_OBJECT:
                putc('{', format->fp);
		if (format->pretty || format->elem) {
                        putc('\n', format->fp);
                        for (scan = scan->first; scan; scan = scan->next) {
                                jcprint(scan, indent + format->tab, format);
			}
                        if (indent > 0)
                                fprintf(format->fp, "%*s", indent, "");
                } else {
                        for (scan = scan->first; scan; scan = scan->next) {
                                jcprint(scan, 0, format);
			}
                }
                putc('}', format->fp);
		break;

	  case JSON_ARRAY:
		putc('[', format->fp);
		if (format->pretty || format->elem) {
			jsonformat_t byelem;
			putc('\n', format->fp);
			byelem = *format;
			if (format->elem) {
				byelem.tab = 0;
				byelem.pretty = 0;
				byelem.elem = 0;
			}
                        for (scan = scan->first; scan; scan = scan->next) {
                                if (format->elem && indent + format->tab > 0)
					fprintf(format->fp, "%*c", indent + format->tab, ' ');
                                jcprint(scan, indent + format->tab, &byelem);
				if (format->elem)
					putc('\n', format->fp);
			}
                        if (indent > 0)
                                fprintf(format->fp, "%*s", indent, "");
                } else {
                        for (scan = scan->first; scan; scan = scan->next) {
                                jcprint(scan, indent + format->tab, format);
			}
		}
		putc(']', format->fp);
		break;

	  case JSON_STRING:
		str = json_serialize(scan, format);
		fputs(str, format->fp);
		free(str);
		break;

	  case JSON_NUMBER:
		/* could be binary int or double, or it could be text */
		if (scan->text[0] == '\0' && scan->text[1] == 'i')
			fprintf(format->fp, "%d", JSON_INT(scan));
		else if (scan->text[0] == '\0' && scan->text[1] == 'd')
			fprintf(format->fp, "%.12g", JSON_DOUBLE(scan));
		else
			fputs(scan->text, format->fp);
		break;

	  case JSON_BOOL:
		fputs(scan->text, format->fp);
		break;

	  case JSON_NULL:
		fputs("null", format->fp);
		break;

	  default:
	  	; /* shouldn't happen */
	}

	if (json->next)
		putc(',', format->fp);
	if (format->pretty || format->elem)
		putc('\n', format->fp);
}

/* Output each row of a table as a line containing a series of name=value pairs */
static void jcsh(json_t *json, jsonformat_t *format){
	json_t	*row;
	json_t	*col;
	char	*s, *t, *frees;

	for (row = json->first; row; row = row->next) {
		for (col = row->first; col; col = col->next) {
			/* Output the prefix, name, and an = */
			fprintf(format->fp, "%s%s=", format->prefix, col->text);

			/* Get the value */
			frees = NULL;
			if (col->first->type == JSON_STRING)
				s = col->first->text;
			else if (json_is_null(col->first))
				s = format->null;
			else
				s = frees = json_serialize(col->first, format);

			/* Does it need quotes? */
			for (t = s; *t; t++)
				if (!isalnum(*t) && *t != '.' && *t != '-')
					break;

			/* Output it, maybe with quotes */
			if (*t)
				putc('\'', format->fp);
			for (; *s; s++) {
				if ((unsigned char)*s < ' ' || *s == '\177')
					; /* omit all control characters */
				else if (*s == '\'') {
					/* To output a ' we must end quoting,
					 * output \' and start new quoting.
					 */
					putc('\'', format->fp);
					putc('\\', format->fp);
					putc('\'', format->fp);
					putc('\'', format->fp);
				} else if ((*s & 0x80) != 0 && format->ascii) {
					char buf[13], *c;
					s = (char *)json_mbs_ascii(s, buf);
					s--; /* because for-loop does s++ */
					for (c = buf; *c; c++)
						putc(*c, format->fp);
				} else
					putc(*s, format->fp);
			}
			if (*t)
				putc('\'', format->fp);

			/* If supposed to free it, do that */
			if (frees)
				free(frees);

			/* If not the last, then output a space */
			if (col->next)
				putc(' ', format->fp);
		}
		putc('\n', format->fp);
	}
}

/* Output a single number, string, or symbol in CSV notation */
static void jccsvsingle(json_t *elem, jsonformat_t *format)
{
	char	*s;

	/* Defend against NULL */
	if (!elem)
		return;

	/* Output values in a type-dependent way */
	switch (elem->type) {
	case JSON_NUMBER:
		/* could be binary int or double, or it could be text */
		if (elem->text[0] == '\0' && elem->text[1] == 'i')
			fprintf(format->fp, "%d", JSON_INT(elem));
		else if (elem->text[0] == '\0' && elem->text[1] == 'd')
			fprintf(format->fp, "%.12g", JSON_DOUBLE(elem));
		else
			fputs(elem->text, format->fp);
		break;
	case JSON_BOOL:
		fputs(elem->text, format->fp);
		break;
	case JSON_NULL:
		fputs(format->null, format->fp);
		break;
	case JSON_STRING:
		putc('"', format->fp);
		for (s = elem->text; *s; s++) {
			if (*s == '\n') {
				putc('\r', format->fp);
				putc('\n', format->fp);
			} else if ((unsigned char)*s < ' ' || *s == '\177') {
				/* omit control characters */
			} else if (*s == '"' || *s == '\\') {
				putc('\\', format->fp);
				putc(*s, format->fp);
			} else if ((*s & 0x80) != 0 && format->ascii) {
				char buf[13], *c;
				s = (char *)json_mbs_ascii(s, buf);
				s--; /* because for-loop does s++ */
				for (c = buf; *c; c++) {
					if (*c == '\\')
						putc('\\', format->fp);
					putc(*c, format->fp);
				}
			} else {
				putc(*s, format->fp);
			}
		}
		putc('"', format->fp);
		break;
	default:
		;/* Omit arrays/objects that are part of a "mixed" column */
	}
}

static void jccsv(json_t *json, jsonformat_t *format) {
	json_t	*headers;
	json_t	*row, *col;
	char	*t;
	int	first;

	/* Collect column names */
	headers = json_explain(NULL, json->first, 0);
	if (!format->quick) {
		for (row = json->first->next; row; row = row->next)
			headers = json_explain(headers, row, 0);
	}

	/* Output column names */
	for (col = headers->first, first = 1; col; col = col->next) {
		/* Skip arrays and objects */
		t = json_text_by_key(col, "type");
		if (!strcmp("table", t) || !strcmp("array", t) || !strncmp("object", t, 6))
			continue;

		/* Comma before all but first */
		if (!first)
			putc(',', format->fp);
		first = 0;

		/* Output the key */
		jccsvsingle(json_by_key(col, "key"), format);
	}
	putc('\n', format->fp);

	/* For each row... */
	for (row = json->first; row; row = row->next) {
		/* Output each column.  Do it in header order, for consistency*/
		for (col = headers->first, first = 1; col; col = col->next) {
			/* Skip arrays and objects */
			t = json_text_by_key(col, "type");
			if (!strcmp("table", t) || !strcmp("array", t) || !strncmp("object", t, 6))
				continue;

			/* Comma before all but first */
			if (!first)
				putc(',', format->fp);
			first = 0;

			/* Output the data */
			jccsvsingle(json_by_key(row, json_text_by_key(col, "key")), format);
		}
		putc('\n', format->fp);
	}
}

/* When writing to stdout, this line indicates whether the last character
 * written was a newline.  This matters ONLY because the GNU readline()
 * function misbehaves if the cursor doesn't start at the begining of a line.
 * Dependence on this variable isn't threadsafe, but neither is readline().
 */
int json_print_incomplete_line;

/* Print a json_t tree as JSON text.  "format" is a combination of values from
 * the JSON_FORMAT_XXXX macros, most importantly JSON_FORMAT_INDENT(n).
 * Returns 1 if the output did NOT end with a newline, or 0 if it did.
 */
void json_print(json_t *json, jsonformat_t *format)
{
	jsonformat_t tweaked;

	/* If NULL pointer then don't print anything (not even "null") */
	if (!json)
		return;

	/* Create a tweakable copy of the format */
	if (!format)
		format = &json_format_default;
	tweaked = *format;
	if (tweaked.fp == NULL)
		tweaked.fp = stdout;

	/* Maybe output error messages embedded in "null" */
	if (tweaked.error && json->type == JSON_NULL && *json->text) {
		if (tweaked.color && isatty(fileno(stderr)))
			fprintf(stderr, "%s%s%s\n",
				json_format_color_error,
				json->text,
				json_format_color_end);
		else
			fprintf(stderr, "%s\n", json->text);
		/* ... but either way, continue to output "null" too */
	}

	/* If not writing to a tty then always inhibit colors */
	if (!isatty(fileno(tweaked.fp)))
		tweaked.color = 0;

	/* Maybe output strings unadorned (and without adding a newline) */
	if (tweaked.string && json->type == JSON_STRING) {
		size_t len;
		fputs(json->text, tweaked.fp);
		len = strlen(json->text);
		if (tweaked.fp == stdout) {
		if (len > 0 && json->text[len - 1] != '\n')
			json_print_incomplete_line = (len > 0 && json->text[len - 1] != '\n');
		}
		return;
	}

	/* Maybe treat short like compact */
	if (tweaked.oneline > 0 && json_is_short(json, tweaked.oneline))
	        tweaked.oneline = tweaked.pretty = 0;

	/* If quoting for the shell, disable pretty */
	if (tweaked.sh)
	        tweaked.oneline = tweaked.pretty = 0;

	/* Table output? */
	if (strchr("scg", tweaked.table) && json_is_table(json)){
		switch (tweaked.table) {
		case 's': jcsh(json, &tweaked);		break;
		case 'c': jccsv(json, &tweaked);	break;
		case 'g': json_grid(json, &tweaked);	break;
		}

		/* Table output always ends with a newline */
		if (tweaked.fp == stdout)
			json_print_incomplete_line = 0;
		return;
	}

	/* Output it as JSON, possibly "pretty" */
	if (tweaked.sh)
		putc('\'', tweaked.fp);
	jcprint(json, 0, &tweaked);
	if (tweaked.sh)
		putc('\'', tweaked.fp);

	/* "Pretty" mode always ends with a newline, but for non-"pretty"
	 * we want to add a newline now.
	 */
	if (!tweaked.pretty)
		fputc('\n', tweaked.fp);
	if (tweaked.fp == stdout)
		json_print_incomplete_line = 0;
}
