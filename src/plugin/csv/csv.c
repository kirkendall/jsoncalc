#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <jx.h>

static char *csvsettings = "{"
	"\"backslash\":true,"	/* Use \x sequences when generating CSV */
	"\"crlf\":false,"	/* Output \r\n as newlines, instead of \n */
	"\"headless\":false,"	/* First row is data, not column headings */
	"\"emptynull\":false,"	/* Parse empty/missing cells as null, not "" */
	"\"pad\":false"		/* Pad short rows to full width of headings */
"}";

/*****************************************************************************/
/* CSV output                                                                */
/*****************************************************************************/

static int backslash;
static int crlf;
static int headless;

/* Output a single number, string, or symbol in CSV notation */
static void csvsingle(jx_t *elem, jxformat_t *format)
{
	char	*s;

	/* Defend against NULL */
	if (!elem)
		return;

	/* Output values in a type-dependent way */
	switch (elem->type) {
	case JX_NUMBER:
		/* could be binary int or double, or it could be text */
		if (elem->text[0] == '\0' && elem->text[1] == 'i')
			fprintf(format->fp, "%d", JX_INT(elem));
		else if (elem->text[0] == '\0' && elem->text[1] == 'd')
			fprintf(format->fp, "%.*g", format->digits, JX_DOUBLE(elem));
		else
			fputs(elem->text, format->fp);
		break;
	case JX_BOOLEAN:
		fputs(elem->text, format->fp);
		break;
	case JX_NULL:
		fputs(format->null, format->fp);
		break;
	case JX_STRING:
		putc('"', format->fp);
		for (s = elem->text; *s; s++) {
			if (*s == '\n') {
				if (crlf)
					putc('\r', format->fp);
				putc('\n', format->fp);
			} else if ((unsigned char)*s < ' ' || *s == '\177') {
				/* omit control characters */
			} else if (backslash && (*s == '"' || *s == '\\')) {
				putc('\\', format->fp);
				putc(*s, format->fp);
			} else if (*s == '"') {
				putc('"', format->fp);
				putc('"', format->fp);
			} else if ((*s & 0x80) != 0 && format->ascii) {
				char buf[13], *c;
				s = (char *)jx_mbs_ascii(s, buf);
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

/* Output a CSV table */
static void csvprint(jx_t *json, jxformat_t *format)
{
	jx_t	*headers;
	jx_t	*row, *col;
	char	*t;
	int	first;

	/* Check options */
	backslash = jx_is_true(jx_config_get("plugin.csv", "backslash"));
	crlf = jx_is_true(jx_config_get("plugin.csv", "crlf"));
	headless = jx_is_true(jx_config_get("plugin.csv", "headless"));

	/* Collect column names */
	headers = jx_explain(NULL, json->first, 0);
	if (!format->quick) {
		for (row = json->first->next; row; row = row->next)
			headers = jx_explain(headers, row, 0);
	}

	/* Output column names, unless headless */
	if (!headless) {
		for (col = headers->first, first = 1; col; col = col->next) {
			/* Skip arrays and objects */
			t = jx_text_by_key(col, "type");
			if (!strcmp("table", t) || !strcmp("array", t) || !strncmp("object", t, 6))
				continue;

			/* Comma before all but first */
			if (!first)
				putc(',', format->fp);
			first = 0;

			/* Output the key */
			csvsingle(jx_by_key(col, "key"), format);
		}
		putc('\n', format->fp);
	}

	/* For each row... */
	for (row = json->first; row && !jx_interrupt; row = row->next) {
		/* Output each column.  Do it in header order, for consistency*/
		for (col = headers->first, first = 1; col; col = col->next) {
			/* Skip arrays and objects */
			t = jx_text_by_key(col, "type");
			if (!strcmp("table", t) || !strcmp("array", t) || !strncmp("object", t, 6))
				continue;

			/* Comma before all but first */
			if (!first)
				putc(',', format->fp);
			first = 0;

			/* Output the data */
			csvsingle(jx_by_key(row, jx_text_by_key(col, "key")), format);
		}
		putc('\n', format->fp);
	}

	/* Free the column data */
	jx_free(headers);
}

/*****************************************************************************/
/* CSV Parser                                                                */

static int emptynull;
static int pad;

/* Parse a single CSV cell and return it as JSON.  If refend isn't NULL, then
 * store the pointer to the comma or newline after the cell there.  If the
 * cell text appears to be malformed, return NULL.
 */
static jx_t *csvcell(const char *buf, size_t len, const char **refcursor)
{
	const char *cursor = *refcursor;
	size_t clen, i, j;
	int	digits;
	jx_t	*cell;

	/* Skip whitespace */
	while (cursor < &buf[len] && *cursor == ' ')
		cursor++;

	/* If invalid character (including CR not part of CRLF), return NULL. */
	if (*cursor == '\r'&& cursor[1] == '\n')
		cursor++;
	*refcursor = cursor;
	if (*cursor < ' ' && *cursor != '\n') {
		return NULL;
	}

	/* If nothing, return null or "" */
	if (*cursor == ',' || *cursor == '\n') {
		if (emptynull)
			return jx_null();
		else
			return jx_string("", 0);
	}

	/* Is it quoted? */
	if (*cursor == '"') {
		/* Find the closing quote */
		for (clen = 1; &cursor[clen] != &buf[len]; clen++) {
			if (cursor[clen] == '"' && cursor[clen + 1] != '"')
				break;
			else if (cursor[clen] == '"') /* && [clen + 1] == '"' */
				clen++;
			else if (cursor[clen] == '\\')
				clen++;
			else if (cursor[clen] == '\0')
				return NULL;
		}

		/* If no closing quote, it's malformed */
		if (&cursor[clen] == &buf[len])
			return NULL;

		/* Allocate a string to hold it.  The clen is actually at least
		 * one character too high (for quotes).  Backslash escapes and
		 * double-quotes may also included in clen, but the excess is
		 * not enough to worry about.
		 */
		cell = jx_string("", clen - 1);

		/* Copy the text into the string */
		for (i = 1, j = 0; i < clen; ) {
			if (cursor[i] == '"' && cursor[i + 1] == '"') {
				cell->text[j++] = '"';
				i += 2;
			} else if (cursor[i] == '\\') {
				switch (cursor[i + 1]) {
				case 'b': cell->text[j++] = '\b'; break;
				case 'f': cell->text[j++] = '\f'; break;
				case 'n': cell->text[j++] = '\n'; break;
				case 'r': cell->text[j++] = '\r'; break;
				case 't': cell->text[j++] = '\t'; break;
				default:
					if (isalpha(cursor[i + 1])) {
						jx_free(cell);
						return NULL;
					}
					cell->text[j++] = cursor[i + 1];
				}
				i += 2;
			} else {
				cell->text[j++] = cursor[i++];
			}
		}
		cell->text[j] = '\0';

		/* Skip past any trailing whitespace */
		cursor += clen + 1;
		while (cursor < &buf[len] && *cursor == ' ')
			cursor++;
	} else {
		/* Count its length */
		for (clen = 1; &cursor[clen] != &buf[len]; clen++) {
			if (cursor[clen] == ',' || (cursor[clen] & 0xff) < ' ')
				break;
		}

		/* Trim trailing whitespace */
		for (i = clen; i > 0 && cursor[i - 1] == ' '; i--) {
		}

		/* Does it look like a number? */
		j = 0;
		digits = 0;
		if (cursor[j] == '-')
			j++;
		for (; j < i && isdigit(cursor[j]); j++)
			digits++;
		if (cursor[j] == '.') {
			j++;
			for (; j < i && isdigit(cursor[j]); j++)
				digits++;
		}
		if (digits && j == i) {
			/* Yes, it's a number */
			cell = jx_number(cursor, i);
		} else {
			/* It's a string */
			cell = jx_string(cursor, i);
		}

		/* Move past the cell and any trailing whitespace */
		cursor += clen;
	}

	/* We expect to end on a comma or newline.  If we got a CR then it
	 * might be part of a CRLF.
	 */
	if (cursor < &buf[len] && *cursor == '\r')
		cursor++;

	/* Return the cell */
	*refcursor = cursor;
	return cell;
}

/* Parse a CSV row and return it as either a JSON array (if columns is NULL) or
 * a JSON object (if columns is an array of column names).  If refend isn't
 * NULL then store a pointer to the final newline there.  If the row is
 * malformed then return a JSON error null.  If no more data is available,
 * it returns NULL.
 *
 * The idea is that you'll call this first with NULL for columns to read the
 * column heading row as an array, and then pass that array as columns for the
 * remainder of the rows.
 */
static jx_t *csvrow(const char *buf, size_t len, const char **refcursor, jx_t *columns)
{
	jx_t	*row, *col, *cell;

	/* If end of data, then return NULL.  This can mean hitting the end of
	 * buf, or encountering an empty line.
	 */
	while (*refcursor < &buf[len] && **refcursor == ' ')
		(*refcursor)++;
	while (*refcursor < &buf[len] && **refcursor == '\r' && (*refcursor)[1] == '\n')
		(*refcursor)++;
	if (*refcursor == &buf[len] || !**refcursor || **refcursor == '\n')
		return NULL;

	/* Allocate the row */
	if (columns) {
		row = jx_object();
		col = columns->first;
	} else {
		row = jx_array();
		col = NULL;
	}

	/* For each cell of the row... */
	while (*refcursor < &buf[len] && **refcursor != '\n') {
		/* Fetch the cell contents */
		cell = csvcell(buf, len, refcursor);

		/* If malformed, then and return an error */
		if (!cell) {
			jx_free(row);
			if (col)
				return jx_error_null(0, "Malformed %s data for \"%s\" column", "csv", col->text);
			return jx_error_null(0, "Malformed %s data in header", "csv");
		}

		/* If more data cells than column headings, return an error */
		if (columns && !col) {
			jx_free(row);
			return jx_error_null(0, "Too many %s data columns", "csv");
		}

		/* Append it to the row */
		if (columns) {
			jx_append(row, jx_key(col->text, cell));
			col = col->next;
		} else
			jx_append(row, cell);

		/* csvcell() leaves the cursor on the ending comma.  Move past
		 * that for the next cell.
		 */
		if (*refcursor < &buf[len] && **refcursor == ',')
			(*refcursor)++;
	}

	/* If this row is short, and we're supposed to pad short rows, do it */
	while (pad && col) {
		if (emptynull)
			cell = jx_null();
		else
			cell = jx_string("", 0);
		jx_append(row, jx_key(col->text, cell));
		col = col->next;
	}

	/* Move past the newline. */
	if (*refcursor < &buf[len] && **refcursor == '\n')
		(*refcursor)++;

	/* Return the row */
	return row;
}

/* Return 1 if "str" appears to be CSV, else return 0 */
static int csvtest(const char *str, size_t len)
{
	jx_t *columns, *data;
	const char *cursor;

	/* Read the column headings */
	cursor = str;
	columns = csvrow(str, len, &cursor, NULL);

	/* If malformed or no info, then it isn't CSV */
	if (!columns || columns->type == JX_NULL || jx_length(columns) < 2) {
		jx_free(columns);
		return 0;
	}

	/* Read the first data row */
	data = csvrow(str, len, &cursor, NULL);

	/* If malformed, no info, or too many data cells, then it isn't CSV */
	if (!data || data->type == JX_NULL || jx_length(columns) < jx_length(data)) {
		jx_free(columns);
		jx_free(data);
		return 0;
	}

	/* It looks like CSV to me.  Clean up and return 1 */
	jx_free(columns);
	jx_free(data);
	return 1;
}

/* Parse "buf" and return its contents as a JSON table.  Store a pointer to
 * the end of the parsed text at "refend" unless "refend" is NULL.  If an error
 * is detected, store a pointer to the location of the error at "referr" (if
 * "referr" is not NULL) and return a jx_t containing an error null.
 */
static jx_t *csvparse(const char *buf, size_t len, const char **refend, const char **referr)
{
	jx_t *columns = NULL, *row, *table;
	const char *cursor;

	/* Check options */
	headless = jx_is_true(jx_config_get("plugin.csv", "headless"));
	emptynull = jx_is_true(jx_config_get("plugin.csv", "emptynull"));
	pad = jx_is_true(jx_config_get("plugin.csv", "pad"));

	/* If headless, then just parse each row as an array, and append the
	 * row's array as an element to the document's array.  The result
	 * isn't a "table" as jx defines it, but it's the best we can do.
	 */
	if (headless) {
		table = jx_array();
		cursor = buf;
		while (cursor < &buf[len] && *cursor != '\n') {
			row = csvrow(buf, len, &cursor, columns);
			if (!row)
				break;
			jx_append(table, row);
		}
		return table;
	}

	/* Parse the heading row */
	cursor = buf;
	columns = csvrow(buf, len, &cursor, NULL);
	if (!columns || columns->type == JX_NULL || jx_length(columns) < 2) {
		/* Store the cursor position */
		if (*refend)
			*refend = cursor;
		if (*referr)
			*referr = cursor;

		/* If we got an error, return the error */
		if (columns && columns->type == JX_NULL && !*columns->text)
			return columns; /* an error */

		/* Otherwise, generate an error and return it */
		jx_free(columns);
		return jx_error_null(0, "Unable to parse \"%s\" data", "csv");
	}

	/* Parse each data row, and collect as an array of objects */
	table = jx_array();
	while (cursor < &buf[len] && *cursor != '\n') {
		row = csvrow(buf, len, &cursor, columns);
		if (!row)
			break;
		if (row->type == JX_NULL && *row->text) {
			jx_free(table);
			return row;
		}
		jx_append(table, row);
	}

	/* Clean up and return the data */
	jx_free(columns);
	if (refend)
		*refend = cursor;
	return table;
}

/*****************************************************************************/

/* This is the init function.  It registers all of the above functions, and
 * adds some constants to the Math object.
 */
char *plugincsv()
{
	jx_t	*section, *settings;

	/* Add options for CSV */
	section = jx_by_key(jx_config, "plugin");
	settings = jx_parse_string(csvsettings);
	jx_append(section, jx_key("csv", settings));

	/* Register the functions */
	jx_print_table_hook("csv", csvprint);
	jx_parse_hook("csv", "csv", ".csv", "text/csv", csvtest, csvparse, NULL);

	/* Success */
	return NULL;
}
