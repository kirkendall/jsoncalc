#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include <jx.h>

/* NOTE: The jx_is_table() function is defined in is.c */

/* If json appears to be a table, then output it as a table/grid.  */
void jx_grid(jx_t *json, jxformat_t *format)
{
	jx_t	*explain, *row, *col, *cell;
	char	*text;
	int	wdata, width, c, i, line;
	int	rowheight, cellheight;
	size_t	size;
	char	hdrpad;
	char	number[40];
	char	*bar, *barface, *cellface;
	int	*widths, *pad;
	jxformat_t tweaked;

	/* If not a table, return 0 */
	if (!jx_is_table(json))
		return;

	/* If format is NULL then use the default format */
	if (!format)
		format = &jx_format_default;
	tweaked = *format;
	format = &tweaked;
	if (!format->fp) /* Default output is stdout */
		format->fp = stdout;
	if (!isatty(fileno(format->fp))) /* Disable color if not a tty */
		format->color = 0;

	/* Collect statistics about the columns.  For deferred arrays,
	 * we may want to limit the number of rows that we check.
	 */
	explain = NULL;
	if (jx_is_deferred_array(json)) {
		/* Get the limit on explain rows to check for deferred arrays.
		 * If >=1 then only scan those rows.
		 */
		int deferexplain = 0;
		jx_t *jdef = jx_by_key(jx_config, "deferexplain");
		if (jdef && jdef->type == JX_NUMBER)
			deferexplain = jx_int(jdef);
		if (deferexplain > 0) {
			/* Collect statistics about columns in the first few rows */
			for (row = jx_first(json);
			     deferexplain > 0 && row;
			     deferexplain--, row = jx_next(row)) {
				explain = jx_explain(explain, row, 0);
			}
			jx_break(row);
		}
	}
	if (!explain) {
		/* Collect column statistics across all rows */
		for (row = jx_first(json); row; row = jx_next(row))
			explain = jx_explain(explain, row, 0);
	}

	/* Allocate arrays to hold padding tips. */
	c = jx_length(explain);
	widths = (int *)calloc(c, sizeof(int));
	pad = (int *)calloc(c, sizeof(int));

	/* If any column's key is wider than their data, expand the column. */
	rowheight = 1;
	for (c = 0, col = jx_first(explain); col; c++, col = jx_next(col)) {
		/* For columns that can contain arrays or objects, make sure
		 * it's wide enough to show "[array]" or "{object}".
		 */
		text = jx_text_by_key(col, "type");
		width = widths[c] = jx_int(jx_by_key(col, "width"));
		if ((!strcmp(text, "array") || !strcmp(text, "table")) && width < 7)
			width = widths[c] = 7;
		else if ((!strcmp(text, "object") || !strcmp(text, "any")) && width < 8)
			width = widths[c] = 8;

		/* For nullable columns, if null isn't displayed as "" then
		 * make sure the column is wide enough for it.
		 */
		if (*format->null && jx_is_true(jx_by_key(col, "nullable"))) {
			int w = jx_mbs_width(format->null);
			if (width < w)
				width = widths[c] = w;
		}

		/* Expand the column if key is wide */
		text = jx_text_by_key(col, "key");
		wdata = jx_mbs_width(text);
		if (wdata > width) {
			pad[c] = wdata - width;
			width = wdata;
		}

		/* If this is the highest key, then increase rowheight */
		cellheight = jx_mbs_height(text);
		if (cellheight > rowheight)
			rowheight = cellheight;
	}

	/* Decide whether to color the output */
	hdrpad = format->color ? ' ' : '_';
	bar = format->graphic ? "\xe2\x94\x82" : "|";

	/* Output the column headings.  If rowheight > 1 we need to do this
	 * separately for each line of the headings.
	 */
	for (line = 0; line < rowheight; line++) {
		/* Colorize? */
		cellface = (line == rowheight - 1 ? "gridhead" : "_gridhead");

		/* Output this line of this column's heading */
		for (c = 0, col = jx_first(explain); col; c++, col = jx_next(col)) {
			/* Get this line of the heading, and its width */
			width = widths[c] + pad[c];
			text = jx_text_by_key(col, "key");
			size = jx_mbs_line(text, line, NULL, &text, &wdata);
			if (size > 0)
				size--; /* remove newline */

			/* Output the key as a column heading */
			jx_user_printf(format, cellface, "");
			for (i = 0; i < (width - wdata + 1) / 2; i++)
				jx_user_ch(hdrpad);
			if (size > 0)
				jx_user_printf(format, cellface, "%.*s", size, text);
			for (; i < (width - wdata); i++)
				jx_user_ch(hdrpad);

			/* Bar between columns */
			if (!jx_is_last(col))
				jx_user_printf(format, cellface, "%s", bar);
		}

		/* End the line */
		jx_user_printf(format, "normal", "\n");
	}

	/* For each row... */
	for (row = jx_first(json); row && !jx_interrupt; row = jx_next(row)) {
		/* Find the height of the tallest cell.  All cells are 1
		 * except for strings that contain newlines.
		 */
		rowheight = 1;
		for (c = 0, col = jx_first(explain); col; c++, col = jx_next(col)) {
			cell = jx_by_key(row, jx_text_by_key(col, "key"));
			if (cell && cell->type == JX_STRING) {
				cellheight = jx_mbs_height(cell->text);
				if (cellheight > rowheight)
					rowheight = cellheight;
			}
		}

		/* For each line of the row... */
		for (line = 0; line < rowheight; line++) {

			/* Choose the color */
			barface = (line == rowheight - 1 ? "gridline" : "_gridline");
			cellface = (line == rowheight - 1 ? "gridcell" : "_gridcell");

			/* Output this line of the row */
			for (c = 0, col = jx_first(explain); col; c++, col = jx_next(col)) {
				/* Fetch the cell */
				cell = jx_by_key(row, jx_text_by_key(col, "key"));
				/* Get its text and width.  Since strings can
				 * be multi-line, they're handled differently.
				 */
				if (cell && cell->type == JX_STRING) {
					size = jx_mbs_line(cell->text, line, NULL, &text, &wdata);
					if (size > 0)
						size--; /* remove newline */
				} else if (line > 0) {
					/* All non-strings are 1 row high */
					size = 0;
					text = "";
					wdata = 0;
				} else {
					if (!cell || cell->type == JX_NULL)
						text = format->null;
					else if (cell->type == JX_ARRAY)
						text = jx_is_table(cell) ? "[table]" : "[array]";
					else if (cell->type == JX_OBJECT)
						text = "{object}";
					else if (cell->type == JX_NUMBER && !cell->text[0] && cell->text[1] == 'i')
						snprintf(text = number, sizeof number, "%d", JX_INT(cell));
					else if (cell->type == JX_NUMBER && !cell->text[0] && cell->text[1] == 'd')
						snprintf(text = number, sizeof number, "%.*g", format->digits, JX_DOUBLE(cell));
					else /* boolean or non-binary number */
						text = cell->text;

					/* Get widths */
					size = strlen(text);
					wdata = jx_mbs_width(text);
				}

				/* width of this column */
				width = widths[c];

				/* If a wide column heading dictates that we
				 * need extra padding (more than data width),
				 * then output half of that extra padding now.
				 */
				if (pad[c] >= 2)
					jx_user_printf(format, cellface, "%*c", pad[c] >> 1, ' ');

				/* Output the cell. Alignment depends on type */
				if (cell && cell->type == JX_STRING) {
					/* left-justify strings */
					if (size > 0)
						jx_user_printf(format, cellface, "%.*s", size, text);
					if (width - wdata > 0)
						jx_user_printf(format, cellface, "%*c", width - wdata, ' ');
				} else if (cell && cell->type == JX_NUMBER) {
					/* right-justify numbers */
					if (width - wdata > 0)
						jx_user_printf(format, cellface, "%*c", width - wdata, ' ');
					jx_user_printf(format, cellface, "%s", text);
				} else {
					/* center everything else */
					if (width - wdata > 0)
						jx_user_printf(format, cellface, "%*c", (width - wdata + 1) >> 1, ' ');
					jx_user_printf(format, cellface, "%s", text);
					if (width - wdata > 1)
						jx_user_printf(format, cellface, "%*c", (width - wdata) >> 1, ' ');
				}

				/* If a wide column heading dictates that we
				 * need extra padding, then output the second
				 * half of that extra padding now.
				 */
				if (pad[c] >= 1)
					jx_user_printf(format, cellface, "%*c", (pad[c] + 1) >> 1, ' ');

				/* Delimiter between columns */
				if (!jx_is_last(col))
					jx_user_printf(format, barface, "%s", bar);

			}
			jx_user_printf(format, "normal", "\n");
		}
	}

	/* Discard the "explain" data */
	jx_free(explain);

	/* Done! */
	return;
}

