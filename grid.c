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
int json_grid(json_t *json, FILE *file, jsonformat_t *format)
{
	json_t	*explain, *row, *col, *cell;
	char	*text;
	int	wdata, width, c, i;
	char	hdrpad;
	char	number[40];
	char	delim[20];
	int	*widths, *pad;

	/* If format is NULL then use the default format */
	if (!format)
		format = &json_format_default;

	/* If not a table, return 0 */
	if (!json_is_table(json))
		return 0;

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
	if (format->color && *json_format_color_head) {
		fputs(json_format_color_head, file);
		hdrpad = ' ';
	}
	if (format->color && *json_format_color_delim) {
		strcpy(delim, json_format_color_delim);
		strcat(delim, "|");
		strcat(delim, json_format_color_end);
	}

	/* If any columns' key are wider than their data, expand the column.
	 * Also, output the column headings while we're at it.
	 */
	for (c = 0, col = explain->first; col; c++, col = col->next) {
		/* Expand the column if key is wide */
		text = json_text_by_key(col, "key");
		wdata = json_mbs_width(text);
		width = widths[c] = json_int(json_by_key(col, "width"));
		if (wdata > width) {
			pad[c] = wdata - width;
			width = wdata;
		}

		/* Output the key as a column heading */
		for (i = 0; i < (width - wdata + 1) / 2; i++)
			putc(hdrpad, file);
		fputs(text, file);
		for (; i < (width - wdata); i++)
			putc(hdrpad, file);
		if (col->next)
			putc('|', file);
	}
	if (format->color && *json_format_color_head)
		fputs(json_format_color_end, file);
	putc('\n', file);

	/* For each row... */
	for (row = json->first; row; row = row->next) {
		/* Output the row */
		for (c = 0, col = explain->first; col; c++, col = col->next) {
			/* Fetch the cell's text */
			cell = json_by_key(row, json_text_by_key(col, "key"));
			if (!cell || (cell->type == JSON_SYMBOL && *cell->text == 'n'))
				text = format->null;
			else if (cell->type == JSON_ARRAY)
				text = "[array]";
			else if (cell->type == JSON_OBJECT)
				text = "{object}";
			else if (cell->type == JSON_NUMBER && !cell->text[0] && cell->text[1] == 'i')
				sprintf(text = number, "%d", JSON_INT(cell));
			else if (cell->type == JSON_NUMBER && !cell->text[0] && cell->text[1] == 'd')
				sprintf(text = number, "%.*g", format->digits, JSON_DOUBLE(cell));
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
				putc(' ', file);

			/* Output the cell.  Alignment depends on type */
			if (cell && cell->type == JSON_STRING) {
				/* left-justify strings */
				fputs(text, file);
				for (i = 0; i < width - wdata; i++)
					putc(' ', file);
			} else if (cell && cell->type == JSON_NUMBER) {
				/* right-justify numbers */
				for (i = 0; i < width - wdata; i++)
					putc(' ', file);
				fputs(text, file);
			} else {
				/* center everything else */
				for (i = 0; i < (width - wdata + 1) / 2; i++)
					putc(' ', file);
				fputs(text, file);
				for (; i < (width - wdata); i++)
					putc(' ', file);
			}

			/* If a wide column heading dictates that we nned
			 * extra padding, then output the second half of
			 * that extra padding now.
			 */
			for (i = pad[c] - (pad[c] >> 1); i > 0; i--)
				putc(' ', file);

			/* Delimiter between columns */
			if (col->next)
				fputs(delim, file);

		}
		putc('\n', file);
	}

	/* Done! */
	return 1;
}

