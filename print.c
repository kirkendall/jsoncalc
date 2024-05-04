#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "json.h"

/* Print a json_t tree as JSON text.  "format" controls the format.  */
static void jcprint(json_t *json, FILE *fp, int indent, jsonformat_t *format)
{
	json_t	*scan;
	char	*str;

	/* output the indent */
	if (indent > 0 && (format->pretty || format->elem))
		fprintf(fp, "%*s", indent, "");

	/* If this is a key, then output the key and switch to its value */
	scan = json;
	if (json->type == JSON_KEY)
	{
                putc('"', fp);
                for (str = json->text; *str; str++) {
			if (*str == '"' || *str == '\\') {
				putc('\\', fp);
			}
			else if (*str == '\'' && format->sh) {
				/* For "sh" format, the entire output is enclosed in
				 * ' quotes which is great for everything except the
				 * ' character itself.  For that, we need to end the
				 * quote, add a backslash-', and start a new quote.
				 */
				putc('\'', fp);
				putc('\\', fp);
				putc('\'', fp);
				putc('\'', fp);
			}
			putc(*str, fp);
		}
                putc('"', fp);
                putc(':', fp);
		scan = json->first;
	}

	switch (scan->type)
	{
	  case JSON_OBJECT:
                putc('{', fp);
		if (format->pretty || format->elem) {
                        putc('\n', fp);
                        for (scan = scan->first; scan; scan = scan->next) {
                                jcprint(scan, fp, indent + format->tab, format);
			}
                        if (indent > 0)
                                fprintf(fp, "%*s", indent, "");
                } else {
                        for (scan = scan->first; scan; scan = scan->next) {
                                jcprint(scan, fp, 0, format);
			}
                }
                putc('}', fp);
		break;

	  case JSON_ARRAY:
		putc('[', fp);
		if (format->pretty || format->elem) {
			jsonformat_t byelem;
			putc('\n', fp);
			byelem = *format;
			if (format->elem) {
				byelem.tab = 0;
				byelem.pretty = 0;
				byelem.elem = 0;
			}
                        for (scan = scan->first; scan; scan = scan->next) {
                                if (format->elem && indent + format->tab > 0)
					fprintf(fp, "%*c", indent + format->tab, ' ');
                                jcprint(scan, fp, indent + format->tab, &byelem);
				if (format->elem)
					putc('\n', fp);
			}
                        if (indent > 0)
                                fprintf(fp, "%*s", indent, "");
                } else {
                        for (scan = scan->first; scan; scan = scan->next) {
                                jcprint(scan, fp, indent + format->tab, format);
			}
		}
		putc(']', fp);
		break;

	  case JSON_STRING:
		str = json_serialize(scan, format);
		fputs(str, fp);
		free(str);
		break;

	  case JSON_NUMBER:
		/* could be binary int or double, or it could be text */
		if (scan->text[0] == '\0' && scan->text[1] == 'i')
			fprintf(fp, "%d", JSON_INT(scan));
		else if (scan->text[0] == '\0' && scan->text[1] == 'd')
			fprintf(fp, "%.12g", JSON_DOUBLE(scan));
		else
			fputs(scan->text, fp);
		break;

	  case JSON_SYMBOL:
		fputs(scan->text, fp);
		break;

	  default:
	  	; /* shouldn't happen */
	}

	if (json->next)
		putc(',', fp);
	if (format->pretty || format->elem)
		fputc('\n', fp);
}

/* Output each row of a table as a line containing a series of name=value pairs */
static void jcsh(json_t *json, FILE *fp, jsonformat_t *format){
	json_t	*row;
	json_t	*col;
	char	*s, *t, *frees;

	for (row = json->first; row; row = row->next) {
		for (col = row->first; col; col = col->next) {
			/* Output the prefix, name, and an = */
			fprintf(fp, "%s%s=", format->prefix, col->text);

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
				putc('\'', fp);
			for (; *s; s++) {
				if ((unsigned char)*s < ' ' || *s == '\177')
					; /* omit all control characters */
				else if (*s == '\'') {
					/* To output a ', we need to end quoting,
					 * output \', and then start new quoting.
					 */
					putc('\'', fp);
					putc('\\', fp);
					putc('\'', fp);
					putc('\'', fp);
				} else if ((*s & 0x80) != 0 && format->ascii) {
					char buf[13], *c;
					s = (char *)json_mbs_ascii(s, buf);
					s--; /* because for-loop does s++ */
					for (c = buf; *c; c++)
						putc(*c, fp);
				} else
					putc(*s, fp);
			}
			if (*t)
				putc('\'', fp);

			/* If supposed to free it, do that */
			if (frees)
				free(frees);

			/* If not the last, then output a space */
			if (col->next)
				putc(' ', fp);
		}
		putc('\n', fp);
	}
}

/* Output a single number, string, or symbol in CSV notation */
static void jccsvsingle(json_t *elem, FILE *fp, jsonformat_t *format)
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
			fprintf(fp, "%d", JSON_INT(elem));
		else if (elem->text[0] == '\0' && elem->text[1] == 'd')
			fprintf(fp, "%.12g", JSON_DOUBLE(elem));
		else
			fputs(elem->text, fp);
		break;
	case JSON_SYMBOL:
		if (json_is_null(elem))
			fputs(format->null, fp);
		else
			fputs(elem->text, fp);
		break;
	case JSON_STRING:
		putc('"', fp);
		for (s = elem->text; *s; s++) {
			if (*s == '\n') {
				putc('\r', fp);
				putc('\n', fp);
			} else if ((unsigned char)*s < ' ' || *s == '\177') {
				/* omit control characters */
			} else if (*s == '"' || *s == '\\') {
				putc('\\', fp);
				putc(*s, fp);
			} else if ((*s & 0x80) != 0 && format->ascii) {
				char buf[13], *c;
				s = (char *)json_mbs_ascii(s, buf);
				s--; /* because for-loop does s++ */
				for (c = buf; *c; c++) {
					if (*c == '\\')
						putc('\\', fp);
					putc(*c, fp);
				}
			} else {
				putc(*s, fp);
			}
		}
		putc('"', fp);
		break;
	default:
		;/* Omit arrays/objects that are part of a "mixed" column */
	}
}

static void jccsv(json_t *json, FILE *fp, jsonformat_t *format) {
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
			putc(',', fp);
		first = 0;

		/* Output the key */
		jccsvsingle(json_by_key(col, "key"), fp, format);
	}
	putc('\n', fp);

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
				putc(',', fp);
			first = 0;

			/* Output the data */
			jccsvsingle(json_by_key(row, json_text_by_key(col, "key")), fp, format);
		}
		putc('\n', fp);
	}
}

/* Print a json_t tree as JSON text.  "format" is a combination of values from
 * the JSON_FORMAT_XXXX macros, most importantly JSON_FORMAT_INDENT(n).
 * Returns 1 if the output did NOT end with a newline, or 0 if it did.
 */
int json_print(json_t *json, FILE *fp, jsonformat_t *format)
{
	jsonformat_t tweaked;

	/* Create a tweakable copy of the format */
	if (!format)
		format = &json_format_default;
	tweaked = *format;

	/* Maybe output strings unadorned (and without adding a newline) */
	if (format->string && json && json->type == JSON_STRING) {
		size_t len;
		fputs(json->text, fp);
		len = strlen(json->text);
		if (len > 0 && json->text[len - 1] != '\n')
			return 1;
		return 0;
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
		case 's': jcsh(json, fp, &tweaked);	break;
		case 'c': jccsv(json, fp, &tweaked);	break;
		case 'g': json_grid(json, fp, &tweaked);break;
		}

		/* Table output always ends with a newline */
		return 0;
	}

	/* Output it as JSON, possibly "pretty" */
	if (tweaked.sh)
		putc('\'', fp);
	jcprint(json, fp, 0, &tweaked);
	if (tweaked.sh)
		putc('\'', fp);

	/* "Pretty" mode always ends with a newline, but for non-"pretty"
	 * we want to add a newline now.
	 */
	if (!tweaked.pretty)
		fputc('\n', fp);
	return 0;
}

/* Pretty-print a JSON expression to stdout */
void json_dump(json_t *json)
{
	json_print(json, stdout, NULL);
}
