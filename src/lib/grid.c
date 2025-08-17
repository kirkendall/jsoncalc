#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <jsoncalc.h>

/* NOTE: The json_is_table() function is defined in is.c */

/* If json appears to be a table, then output it as a table/grid.  */
void json_grid(json_t *json, jsonformat_t *format)
{
	json_t	*explain, *row, *col, *cell;
	char	*text;
	int	wdata, width, c, i, line;
	int	rowheight, cellheight;
	size_t	size;
	char	hdrpad;
	char	number[40];
	char	*bar;
	char	delim[20];
	int	*widths, *pad;
	jsonformat_t tweaked;

	/* If not a table, return 0 */
	if (!json_is_table(json))
		return;

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

	/* If any column's key is wider than their data, expand the column.
	 * Also, output the column headings while we're at it.
	 */
	rowheight = 1;
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

		/* For nullable columns, if null isn't displayed as "" then
		 * make sure the column is wide enough for it.
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

		/* If this is the highest key, then increase rowheight */
		cellheight = json_mbs_height(text);
		if (cellheight > rowheight)
			rowheight = cellheight;
	}

	/* Decide whether to color the output */
	hdrpad = format->color ? ' ' : '_';
	bar = format->graphic ? "\xe2\x94\x82" : "|";
	strcpy(delim, bar);
	if (format->color && *format->escgridline) {
		strcpy(delim, format->escgridline);
		strcat(delim, bar);
		strcat(delim, json_format_color_end);
	}

	/* Output the column headings.  If rowheight > 1 we need to do this
	 * separately for each line of the headings.
	 */
	for (line = 0; line < rowheight; line++) {
		/* Colorize? */
		if (format->color) {
			if (line + 1 == rowheight) {
				/* Last row of headings (usually only row) */
				if (*format->escgridhead)
					fputs(format->escgridhead, format->fp);
			} else {
				/* An earlier line of multi-line headings */
				if (*format->escgridhead2)
					fputs(format->escgridhead2, format->fp);
			}
		}

		/* Output this line of this column's heading */
		for (c = 0, col = explain->first; col; c++, col = col->next) {
			/* Get this line of the heading, and its width */
			width = widths[c] + pad[c];
			text = json_text_by_key(col, "key");
			size = json_mbs_line(text, line, NULL, &text, &wdata);
			if (size > 0)
				size--; /* remove newline */

//fprintf(format->fp, "%02d/%02d", wdata, width);
			/* Output the key as a column heading */
			for (i = 0; i < (width - wdata + 1) / 2; i++)
				putc(hdrpad, format->fp);
			if (size > 0)
				fwrite(text, 1, size, format->fp);
			for (; i < (width - wdata); i++)
				putc(hdrpad, format->fp);
			if (col->next)
				fputs(bar, format->fp);
		}

		/* End the line */
		if (format->color && *format->escgridhead)
			fputs(json_format_color_end, format->fp);
		putc('\n', format->fp);
	}

	/* For each row... */
	for (row = json->first; row && !json_interrupt; row = row->next) {
		/* Find the height of the tallest cell.  All cells are 1
		 * except for strings that contain newlines.
		 */
		rowheight = 1;
		for (c = 0, col = explain->first; col; c++, col = col->next) {
			cell = json_by_key(row, json_text_by_key(col, "key"));
			if (cell && cell->type == JSON_STRING) {
				cellheight = json_mbs_height(cell->text);
				if (cellheight > rowheight)
					rowheight = cellheight;
			}
		}

		/* For each line of the row... */
		for (line = 0; line < rowheight; line++) {

			/* Output this line of the row */
			for (c = 0, col = explain->first; col; c++, col = col->next) {
				/* Fetch the cell */
				cell = json_by_key(row, json_text_by_key(col, "key"));
				/* Get its text and width.  Since strings can
				 * be multi-line, they're handled differently.
				 */
				if (cell && cell->type == JSON_STRING) {
					size = json_mbs_line(cell->text, line, NULL, &text, &wdata);
					if (size > 0)
						size--; /* remove newline */
				} else if (line > 0) {
					/* All non-strings are 1 row high */
					size = 0;
					text = "";
					wdata = 0;
				} else {
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
					else /* boolean or non-binary number */
						text = cell->text;

					/* Get widths */
					size = strlen(text);
					wdata = json_mbs_width(text);
				}

				/* width of this column */
				width = widths[c];

				/* If a wide column heading dictates that we
				 * need extra padding, then output half of that
				 * extra padding now.
				 */
				for (i = pad[c] >> 1; i > 0; i--)
					putc(' ', format->fp);

				/* Output the cell. Alignment depends on type */
				if (cell && cell->type == JSON_STRING) {
					/* left-justify strings */
					if (size > 0)
						fwrite(text, size, 1, format->fp);
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
	}

	/* Discard the "explain" data */
	json_free(explain);

	/* Done! */
	return;
}

