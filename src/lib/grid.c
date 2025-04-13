#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include "json.h"
#include "calc.h"

/* NOTE: The json_is_table() function is defined in is.c */

/* If json appears to be a table, then output it as a table/grid and return 1.
 * Otherwise return 0 without outputting anything
 */
int json_grid(json_t *json, jsonformat_t *format)
{
	json_t	*explain, *row, *col, *cell;
	char	*text;
	int	wdata, width, c, i;
	char	hdrpad;
	char	number[40];
	char	delim[20];
	int	*widths, *pad;
	jsonformat_t tweaked;

	/* If not a table, return 0 */
	if (!json_is_table(json))
		return 0;

	/* If format is NULL then use the default format */
	if (!format)
		format = &json_format_default;
	tweaked = *format;
	format = &tweaked;
	if (!format->fp) /* Default output is stdout */
		format->fp = stdout;
	if (!isatty(fileno(format->fp))) /* Disable color if not a tty */
		format->color = 0;

	/* Collect statistics about the columns */
	explain = NULL;
	for (row = json->first; row; row = row->next)
		explain = json_explain(explain, row, 0);

	/* Allocate arrays to hold padding tips. */
	c = json_length(explain);
	widths = (int *)calloc(c, sizeof(int));
	pad = (int *)calloc(c, sizeof(int));

	/* Decide whether to color the output */
	hdrpad = '_';
	strcpy(delim, "|");
	if (format->color && *format->escgridhead) {
		fputs(format->escgridhead, format->fp);
		hdrpad = ' ';
	}
	if (format->color && *format->escgridline) {
		strcpy(delim, format->escgridline);
		strcat(delim, "|");
		strcat(delim, json_format_color_end);
	}

	/* If any column's key is wider than their data, expand the column.
	 * Also, output the column headings while we're at it.
	 */
	for (c = 0, col = explain->first; col; c++, col = col->next) {
		/* For columns that can contain arrays or objects, make sure
		 * it's wide enough to show "[array]" or "{object}".
		 */
		text = json_text_by_key(col, "type");
		width = widths[c] = json_int(json_by_key(col, "width"));
		if ((!strcmp(text, "array") || !strcmp(text, "table")) && width < 7)
			width = widths[c] = 7;
		else if ((!strcmp(text, "object") || !strcmp(text, "any")) && width < 8)
			width = widths[c] = 8;

		/* For nullable columns, if null isn't displayed as "" then make sure
		 * the column is wide enough for it.
		 */
		if (*format->null && json_is_true(json_by_key(col, "nullable"))) {
			int w = json_mbs_width(format->null);
			if (width < w)
				width = widths[c] = w;
		}

		/* Expand the column if key is wide */
		text = json_text_by_key(col, "key");
		wdata = json_mbs_width(text);
		if (wdata > width) {
			pad[c] = wdata - width;
			width = wdata;
		}

		/* Output the key as a column heading */
		for (i = 0; i < (width - wdata + 1) / 2; i++)
			putc(hdrpad, format->fp);
		fputs(text, format->fp);
		for (; i < (width - wdata); i++)
			putc(hdrpad, format->fp);
		if (col->next)
			putc('|', format->fp);
	}
	if (format->color && *format->escgridhead)
		fputs(json_format_color_end, format->fp);
	putc('\n', format->fp);

	/* For each row... */
	for (row = json->first; row; row = row->next) {
		/* Output the row */
		for (c = 0, col = explain->first; col; c++, col = col->next) {
			/* Fetch the cell's text */
			cell = json_by_key(row, json_text_by_key(col, "key"));
			if (!cell || cell->type == JSON_NULL)
				text = format->null;
			else if (cell->type == JSON_ARRAY)
				text = json_is_table(cell) ? "[table]" : "[array]";
			else if (cell->type == JSON_OBJECT)
				text = "{object}";
			else if (cell->type == JSON_NUMBER && !cell->text[0] && cell->text[1] == 'i')
				snprintf(text = number, sizeof number, "%d", JSON_INT(cell));
			else if (cell->type == JSON_NUMBER && !cell->text[0] && cell->text[1] == 'd')
				snprintf(text = number, sizeof number, "%.*g", format->digits, JSON_DOUBLE(cell));
			else
				text = cell->text;

			/* Get widths */
			width = widths[c];
			wdata = json_mbs_width(text);

			/* If a wide column heading dictates that we need
			 * extra padding, then output half of that extra
			 * padding now.
			 */
			for (i = pad[c] >> 1; i > 0; i--)
				putc(' ', format->fp);

			/* Output the cell.  Alignment depends on type */
			if (cell && cell->type == JSON_STRING) {
				/* left-justify strings */
				fputs(text, format->fp);
				for (i = 0; i < width - wdata; i++)
					putc(' ', format->fp);
			} else if (cell && cell->type == JSON_NUMBER) {
				/* right-justify numbers */
				for (i = 0; i < width - wdata; i++)
					putc(' ', format->fp);
				fputs(text, format->fp);
			} else {
				/* center everything else */
				for (i = 0; i < (width - wdata + 1) / 2; i++)
					putc(' ', format->fp);
				fputs(text, format->fp);
				for (; i < (width - wdata); i++)
					putc(' ', format->fp);
			}

			/* If a wide column heading dictates that we nned
			 * extra padding, then output the second half of
			 * that extra padding now.
			 */
			for (i = pad[c] - (pad[c] >> 1); i > 0; i--)
				putc(' ', format->fp);

			/* Delimiter between columns */
			if (col->next)
				fputs(delim, format->fp);

		}
		putc('\n', format->fp);
	}

	/* Discard the "explain" data */
	json_free(explain);

	/* Done! */
	return 1;
}

