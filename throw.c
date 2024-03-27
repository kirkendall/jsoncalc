#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "json.h"

/* This is the default error handler */
static void json_throw_default(json_parse_t *parse, char *fmt, ...)
{
	va_list ap;
	long	col;

	/* Print the position, if known */
	col = -1;
	if (parse)
	{
		if (parse->file)
			fprintf(stderr, "%s:", parse->file);
		if (parse->lineno)
			fprintf(stderr, "%ld:", parse->lineno);
		if (parse->buf)
		{
			col = (parse->token - parse->buf);
			if (col >= 0 && col < strlen(parse->buf))
				fprintf(stderr, "%ld:", col);
		}
	}

	/* Print the error message */
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);

	/* exit */
	if (json_debug_flags.abort)
		abort();
	exit(1);
}

/* This is a pointer to the error handler */
json_catcher_t json_throw = json_throw_default;

/* Configure the error handler for the JSON library */
json_catcher_t json_catch(json_catcher_t new)
{
	json_catcher_t old = json_throw;
	if (new)
		json_throw = new;
	else
		json_throw = json_throw_default;
	return old;
}
