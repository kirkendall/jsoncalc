/* user.c
 *
 * This contains functions for outputting text to the user.  Normally this
 * involves writing to stdout/stderr, but it can be intercepted by the user
 * interface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <assert.h>
#include <jsoncalc.h>

/* These indicate whether stdout and stderr are connected to a terminal.
 * The alternative is that they're connected to a file/pipe which matters
 * because we don't want to send escape sequences through a file or pipe.
 */
static int stdout_tty;
static int stderr_tty;

/* This points to the member value in json_config for the current set of
 * colors and other attributes.  If none are selected, this will be NULL.
 */
static const char *curstyle;	/* Name of the current style */
static json_t *jstyle;		/* Member of json_config containing style */
static FILE *outfp;		/* stdout or stderr, depending on style */
static int didesc;		/* boolean: Are attributes current set? */

/* This stores a pointer to a function to intercept user output, or NULL */
static int (*user_hook)(json_t *format, int newformat, const char *text, size_t len);

/* This stores a pointer to a function to handle the result of a stand-alone
 * expression.  Some user interfaces will use this to display the result in a
 * data-viewer window.
 */
static int (*result_hook)(json_t *result);

/* Convert a color name or other attribute to a number */
static char *esccode(char *esc, const char *color)
{
	int	i;
	char	*code;

	/* Map names to numbers, for coloring escape sequences */
	static struct {
		char *name;
		char code[3];
	} colors[] = {
		{"bold","1"},{"dim","2"},{"italic","3"},{"underlined","4"},
		{"blinking","5"},{"boxed","7"},{"strike","9"},
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

/* Convert a color from the config object to an escape sequence.  "esc" points
 * to a buffer to receive the escape sequence.  "name" is the name of a color
 * setting, such as "gridhead". 'nounderlined" inhibits underlining if set to 1.
 */
static void esc(char *buf, json_t *style, int nounderlined)
{
	char	*wholebuf = buf;

	/* Find the nested object describing this color */
	if (!style) {
		*buf = '\0';
		return;
	}

	/* Start the escape sequence */
	*buf++ = '\033';
	*buf++ = '[';

	/* Foreground and background */
	buf = esccode(buf, json_text_by_key(style, "fg"));
	buf = esccode(buf, json_text_by_key(style, "bg"));

	/* Other attributes */
	if (json_is_true(json_by_key(style, "bold")))
		buf = esccode(buf, "bold");
	if (json_is_true(json_by_key(style, "dim")))
		buf = esccode(buf, "dim");
	if (json_is_true(json_by_key(style, "italic")))
		buf = esccode(buf, "italic");
	if (!nounderlined && json_is_true(json_by_key(style, "underlined")))
		buf = esccode(buf, "underlined");
	if (json_is_true(json_by_key(style, "blinking")))
		buf = esccode(buf, "blinking");
	if (json_is_true(json_by_key(style, "boxed")))
		buf = esccode(buf, "boxed");
	if (json_is_true(json_by_key(style, "strike")))
		buf = esccode(buf, "strike");

	/* We should have an extra ";" at the end, which we'll convert to
	 * 'm' to complete the sequence.  If there is no ';' then there are
	 * no attributes set so we might as well skip the whole sequence.
	 */
	if (buf[-1] == ';') {
		buf[-1] = 'm';
		buf[0] = '\0';
	} else
		*wholebuf = '\0';
}

/* Write text to the user. "format" is the general format info, including the
 * format->fp field for writing to files.  "style" is the name of a member of
 * json_config * that defines the colors and other attributes of the text; you
 * can prefix it with "_" to inhibit underlining. "fmt" is a printf-style
 * formatting string.
 */
void json_user_printf(jsonformat_t *format, const char *style, const char *fmt, ...)
{
	static char *buf;
	static size_t buflen;
	size_t len;
	va_list ap;
	int	newstyle;
	int	nounderline;
	char	escbuf[32];

	/* If no format specified, use default */
	if (!format)
		format = &json_format_default;

	/* If writing to a file/pipe, then just do it */
	if (format->fp && !isatty(fileno(format->fp))) {
#if 0
		if (didesc) {
			fwrite("\033[m", 1, 3, outfp);
			didesc = 0;
		}
#endif
		outfp = format->fp;
		va_start(ap, fmt);
		vfprintf(format->fp, fmt, ap);
		va_end(ap);
		return;
	}

	/* Initial buffer size guess is fairly modest */
	if (!buf) {
		buflen = 1024;
		buf = (char *)malloc(buflen);

		/* While we're at it, let's figure out whether stdout and
		 * stderr refer to ttys or files/pipes.
		 */
		stdout_tty = isatty(fileno(stdout));
		stderr_tty = isatty(fileno(stderr));
	}

	/* If given a style name, and it's different from what we have now,
	 * then look it up.
	 */
	newstyle = 0;
	if (!style && !curstyle)
		style = "normal";
	if (!curstyle || (style && strcmp(style, curstyle))) {
		/* If we're using escape sequences to change styles, and we
		 * did indeed send one, then turn off that style now.
		 */
		if (didesc) {
			fwrite("\033[m", 1, 3, outfp);
			didesc = 0;
		}

		/* Look up the new style.  If prefixed with "_" then inhibit
		 * underlining.  Note that the style name does not come from
		 * a script or user input; we can trust it to be the name of
		 * an existing style so json_config_style(...NULL) will *NOT*
		 * create a new style.
		 */
		newstyle = 1;
		curstyle = style;
		nounderline = 0;
		if (*style == '_') {
			nounderline = 1;
			style++;
		}
		jstyle = json_config_style(style, NULL);

		/* If the user interface supports writing to stdout/stderr,
		 * then choose which one to write to, based on style.stderr.
		 */
		outfp = (json_is_true(json_by_key(jstyle, "stderr"))) ? stderr : stdout;
	}

	/* Try to print the text into the buffer. */
	if (!fmt || *fmt == '\0')
		len = 0;
	else {
		va_start(ap, fmt);
		len = vsnprintf(buf, buflen, fmt, ap);
		va_end(ap);

		/* If it didn't fit, expand the buffer and try again */
		if (len >= buflen) {
			buflen = (len | 0x3ff) + 1;
			buf = (char *)realloc(buf, buflen);

			va_start(ap, fmt);
			len = vsnprintf(buf, buflen, fmt, ap);
			va_end(ap);
		}
	}

	/* If there's a hook, give it a shot */
	if (user_hook && (*user_hook)(jstyle, newstyle, buf, len))
		return;

	/* Nope, we need to do this the old-school way */

	/* If connected to a tty, then maybe output an escape sequence to
	 * set attributes.
	 */
	if (format->color && (outfp == stdout ? stdout_tty : stderr_tty)) {
		/* Generate the escape sequence */
		esc(escbuf, jstyle, nounderline);

		/* Send it, unless empty */
		if (*escbuf) {
			fputs(escbuf, outfp);
			didesc = 1;
		} else if (didesc) {
			fputs("\033[m", outfp);
			didesc = 0;
		}
	}

	/* Send the text */
	if (len > 0)
		fputs(buf, outfp);
}

/* Write a single character to the user.  If you pass a byte from a multibyte
 * UTF-8 character, this will accumulate all bytes before sending it to the
 * user's terminal or the user interface.  This will use the same attributes
 * as the most recent json_user_printf() call.
 */
void json_user_ch(int ch)
{
	static char buf[10];
	static int used;

	/* If writing to a file, just do it */
	if (!outfp)
		outfp = stdout;
	if (outfp != stdout && outfp != stderr) {
		fputc(ch, outfp);
		return;
	}

	/* Add it to the buffer. */
	buf[used++] = (char)ch;

	/* If multibyte and we need more bytes, then that's all we'll do for now */
	if ((buf[0] & 0xe0) == 0xc0 && used < 2)
		return;
	if ((buf[0] & 0xf0) == 0xe0 && used < 3)
		return;
	if ((buf[0] & 0xf8) == 0xf0 && used < 4)
		return;

	/* Give the hook a chance, if there is one.  Else just write it */
	if (!user_hook || (*user_hook)(jstyle, 0, buf, used))
		fwrite(buf, 1, used, outfp);
	used = 0;
}

/* This is called for anything that's output as the result of an expression
 * on a line by itself (not part of an assignment or other command).  The
 * function returns 0 if the result can be freed, or 1 if the user interface
 * is going to hold on to it for a while and then free it.  The default
 * behavior is to do nothing and return 0, but fancier user interfaces may
 * display the result in a data viewer window.
 */
int json_user_result(json_t *result)
{
	/* If there's a hook, give it a shot */
	if (result_hook && (*result_hook)(result))
		return 1;

	/* Do nothing and return 0, so the result is freed */
	return 0;
}

/* This registers a user interface function to replace the normal writing to
 * stdout/stderr.  The handler returns 0 normally, of 1 if the text should be
 * output in the normal manner (by writing to stdout/stderr).
 */
void json_user_hook(int (*handler)(json_t *jstyle, int newstyle, const char *text, size_t len))
{
	user_hook = handler;
}

/* Register a function to handle the result of stand-alone expressions.  The
 * json_user_result() function calls thisto give the user interface a chance
 * to save the result in a data viewer window or something.  The handler
 * function should return 0 if the result will not be used and should be freed,
 * or 1 if the user interface is using it and wants to be responsible for
 * freeing it later.
 */
void json_user_result_hook(int (*handler)(json_t *result))
{
	result_hook = handler;
}
