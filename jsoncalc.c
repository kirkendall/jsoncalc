#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <glob.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#define JSON_DEBUG_MEMORY
#include "json.h"
#include "calc.h"
#include "version.h"

#define HISTORY_FILE ".jsoncalc_history"
#define AUTOLOAD_DIR "sampledata"


/* getopt() variables not declared in <unistd.h> */
extern char *optarg;
extern int optind, opterr, optopt;

/* -i -- Force interactive mode */
int interactive = 0;

/* -r -- inhibit the readline library? */
int inhibit_readline = 0;

/* -v -- The name of an environment variable to read/set */
char *envname = NULL;

/* -D -- This is a directory to check for autoload */
char *autoload_dir = NULL;

/* This is the context -- a stack of data that is available to expressions. */
jsoncontext_t *context;


/* Output a usage message for the -Oformat option */
static void format_usage()
{
	puts("The -Oformat flag controls the output format.  The \"format\" string is a");
	puts("comma-delimited list of settings.  Each setting is a name=value pair,");
	puts("though if you omit the =value then a default value is used.  For boolean");
	puts("settings the default is true.  You can also turn off a boolean option by");
	puts("giving it a \"no\" prefix.  The list of possible settings is:");
	printf("  -O%-11sPretty print adds whitepsace to show the data's structure.\n", json_format_default.pretty ? "pretty" : "nopretty");
	printf("  -O%-11sForces each array element onto a single line.\n", json_format_default.elem ? "elem" : "noelem");
	printf("  -Otab=%-7dIndentation to use when pretty printing.\n", json_format_default.tab);
	printf("  -Ooneline=%-3dIf >0, JSON strings shorter than this are not pretty-printed.\n", json_format_default.oneline);
	printf("  -Otable=%-5sTable format: grid/csv/sh/json (Tables are arrays of objects.)\n", json_format_default.table=='s'?"sh": json_format_default.table=='c'?"csv": json_format_default.table=='g'?"grid": "json");
	printf("  -O%-11sFor table=csv and table=grid, use the first row to find columns.\n", json_format_default.quick ? "quick" : "noquick");
	printf("  -Oprefix=%-4sFor table=sh, this is prepended to variable names.\n", json_format_default.null);
	printf("  -Onull=%-6sFor table=grid, this is how null will be shown.\n", json_format_default.null);
	printf("  -O%-11sConvert any non-ASCII characters to \\uXXXX sequences.\n", json_format_default.ascii ? "ascii" : "noascii");
	printf("  -O%-11sAdd shell quoting to JSON output.\n", json_format_default.sh ? "sh" : "nosh");
	printf("  -O%-11sAdd color for ANSI terminals.\n", json_format_default.color ? "color" : "nocolor");
	printf("  -Odigits=%-4dPrecision when converting numbers to strings.\n", json_format_default.digits);
}

/* Output a color usage statement */
static void color_usage()
{
	char	*colors;
	puts("The -Ccolors option sets the colors used when -Ocolor is set.  The \"colors\"");
	puts("string is a series of name=value pairs, where the name is a color role");
	puts("(one of result, head, delim, error, or debug) and the value is a description");
	puts("of the color such as \"bold underlined blue on white\"");
	colors = json_format_color_str();
	printf("-C%s\n", colors);
	free(colors);
}

/* Output a debugging flag usage message */
static void debug_usage()
{
	puts("The -Jflags option controls debugging flags.  The flags are single letters");
	puts("indicating what should be debugged.  The flag letters are:");
	puts("  t  Output tokens as the JSON data is parsed.");
	puts("  f  Output information about accessing data files.");
	puts("  b  Output buffer information when JSON data is read in chunks.");
	puts("  a  Call abort() when an error is detected.");
	puts("  e  Output info about json_by_expr() calls.");
	puts("  c  Output info about json_calc() calls.");
	puts("");
	puts("You may also put a + or - or = between -J and the flags to alter the way the new");
	puts("flags are combined with existing flags.  The means are:");
	puts("  +  Add the new flags to existing flags. (This is the default.)");
	puts("  =  Turn off all flags except the new flags.");
	puts("  -  Turn off existing flags that are also in new flags.");
	printf("\nCurrent flags are: -J%s%s%s%s%s%s\n",
		json_debug_flags.token ? "t" : "",
		json_debug_flags.file ? "f" : "",
		json_debug_flags.buffer ? "b" : "",
		json_debug_flags.abort ? "a" : "",
		json_debug_flags.expr ? "e" : "",
		json_debug_flags.calc ? "c" : "");
}

/* Output a usage message and then exit. */
static void usage(char *fmt, char *data)
{
	puts("Usage: jsoncalc [flags] [name=value]... [file.json]...");
	puts("Flags: -c calc    Evaluate calc, output any results, and quit.");
	puts("       -f file    Read calc expression from file, output the result, and quit.");
	puts("       -v env     Read environment variable $env var, output cmd to set it.");
	puts("       -i         Interactive.");
	puts("       -r         Inhibit the use of readline for interactive input.");
	puts("       -u         Update any modified *.json files named on the command line.");
	puts("       -l plugin  Load libjcplugin.so from $JSONCALCPATH or $LIBPATH.");
	puts("       -D dir     Autoload files from directory \"dir\".");
	puts("       -O format  Adjusts the output format. Use -O? for more info.");
	puts("       -C colors  Adjusts colors for ANSI terminals. Use -C? for more info.");
	puts("       -J flags   Debug: t=token, f=file, b=buffer, a=abort, e=expr, c=calc");
	puts("       -U         Save -O, -C, -D, -J flags as default config");
	puts("This program manipulates JSON data. Without one of -ccalc, -ffile, -venv or -u,");
	puts("it will assume -c\"this\" if -Oformat is given, or -i if no -Oformat.  Any");
	puts("name=value parameters on the shell command line will be added to the context,");
	puts("so you can use them like variables. Any *.json files named on the command line");
	puts("will be processed one at a time for -ccalc or -ffile.  For -i, the first *.json");
	puts("is loaded as \"this\" and you can switch to others manually.");
	if (fmt)
		printf(fmt, data);
	exit(1);
}

/* This function is called if you use an unrecognized name in a statement.
 * If the name is "files" then it returns an array of JSON files in the
 * sampledata directory.  If it is the name of one of those files, then it
 * loads the file into memory and returns that.
 */
json_t *autoload(char *key)
{
	char    filename[1000];
	json_t  *json;

	/* If "files" then return an array of file basenames */
	if (!strcmp(key, "files")) {
		glob_t files;
		int     i;
		char    *basename;

		sprintf(filename, "%s/*.json", AUTOLOAD_DIR);
		memset(&files, 0, sizeof files);
		if (glob(filename, 0, NULL, &files) == 0) {
			json = json_array();
			for (i = 0; i < files.gl_pathc; i++) {
				struct stat st;
				json_t *row = json_object();
				basename = strrchr(files.gl_pathv[i], '/');
				if (basename)
					basename++;
				else
					basename = files.gl_pathv[i];
				json_append(row, json_key("key", json_string(basename, strlen(basename) - 5)));
				json_append(row, json_key("filename", json_string(files.gl_pathv[i], -1)));
				stat(files.gl_pathv[i], &st);
				json_append(row, json_key("size", json_from_int(st.st_size)));
				json_append(json, row);
			}
			globfree(&files);
			return json;
		}
		return NULL;
	}

	/* Is there a key.json file? */
	sprintf(filename, "%s/%s.json", AUTOLOAD_DIR, key);
	if (access(filename, R_OK) != 0)
		return NULL;

	/* Try to parse the file */
	json = json_parse_file(NULL, filename);
	return json;
}




/*****************************************************************************/
/* The following code supports name completion in the readline() function.   */

/* This tries to complete a global name by scanning the context. */
char *global_name_generator(const char *text, int state)
{
	static size_t len;
	static json_t *scan;
	static jsoncontext_t *con;

	/* First time? */
	if (state == 0) {
		/* Start with the newest context */
		con = context;
		scan = NULL;
		len = json_mbs_len(text);
	}

	/* If scan is NULL, then move it to the start of current context
	 * except that if the context's data isn't an object then skip it.
	 * If scan is non-NULL then just try moving to the next.
	 */
	if (!scan) {
		/* Skip non-object contexts */
		while (con && con->data->type != JSON_OBJECT)
			con = con->older;
		if (!con)
			return NULL;
		scan = con->data->first;
	} else {
		scan = scan->next;
	}

	/* Now loop, looking for the next partial match.  If found, return it.
	 * If not found and we reach the end, then return NULL.
	 */
	for (;;) {
		for (; scan; scan = scan->next) {
			if (!json_mbs_ncmp(scan->text, text, len)) {
				if (scan->first->type == JSON_OBJECT)
					rl_completion_append_character = '.';
				else
					rl_completion_suppress_append = 1;
				return strdup(scan->text);
			}
		}
		do {
			con = con->older;
		} while (con && con->data->type != JSON_OBJECT);
		if (!con)
			return NULL;
		scan = con->data->first;
	}
}

/* For member completions, completion_container is set by jsoncalc_completion()
 * to an object whose member names are to be scanned.  completion_key is the
 * partial key to look for -- a pointer into readline's line buffer.  This
 * can be combined with the "text" parameter to determine the whole string
 * to return.
 */
json_t	*completion_container;
int	completion_key_offset;

/* Return the first (if state=0) or next (if state>0) name that matches text.
 * If there are no more matches, return NULL.  The returned value should be
 * dynamically allocated, e.g. via strdup(); readline() will free it.
 */
char *member_name_generator(const char *text, int state)
{
	static json_t *scan;
	char	buf[1000];
	size_t	len;

	/* Make a mutable copy of text */
	strcpy(buf, text);

	/* If first, then reset scan */
	if (!state)
		scan = completion_container->first;
	else
		scan = scan->next;

	/* We're looking in a container (object) */
	len = json_mbs_len(text + completion_key_offset);
	for (; scan; scan = scan->next) {
		if (!json_mbs_ncmp(scan->text, text + completion_key_offset, len)) {
			if (scan->first->type == JSON_OBJECT)
				rl_completion_append_character = '.';
			else if (scan->first->type == JSON_ARRAY)
				rl_completion_append_character = '[';
			else
				rl_completion_suppress_append = 1;
			strcpy(&buf[completion_key_offset], scan->text);
			return strdup(buf);
		}
	}

	return NULL;
}

/* This collects all matching names into a dynamically allocated array of
 * dynamically allocated strings, and returns the array.  "text" is a pointer
 * to the start of the text within readline()'s input buffer.  "start" is
 * the number of additional characters available before "text".  "end"
 * is the index of the end of "text" though "text" is NUL-terminated so it
 * isn't all than necessary.
 */
char **jsoncalc_completion(const char *text, int start, int end)
{
	char	*key, *next, c;
	int	scan, state;
	char	buf[1000];

	/* Make a non-const copy of the whole line (not just "text") */
	strcpy(buf, rl_line_buffer);
	buf[end] = '\0';

	/* We either get "na" or "name.na" or ".name.na".  The first completes
	 * by looking for names in the context.  The second starts by fetching
	 * the value of a global name from the context, and then steps down
	 * through the expression until the end, where we have a container
	 * (object, hopefully) and a partial name to look for in it.  The third
	 * form is preceded by something too complex for readline() to handle
	 * for us, but if it's a subscript then it might be preceded by other
	 * names, starting with a global name.
	 */

	/* Let's start with the easy case: global name completion */
	if (!strchr(text, '.')) {
		/* No dots, must be partial global */
		return rl_completion_matches(text, global_name_generator);
	}

	/* If the given text starts with "." then check to see if it's preceded
	 * by a subscript and names.
	 */
	if (*text == '.') {
		/* If not preceded by a subscript and names, we can't help */
		if (start < 3 || buf[start - 1] != ']') {
			return NULL;
		}

		/* Skip back over any subscripts (even if complex) and any
		 * names/dots outside of subscripts.
		 */
		for (scan = start - 1, state = 0; state >= 0; scan--) {
			if (scan == 0) {
				if (state > 0)
					return NULL;
				break;
			}
			if (buf[scan] == '[')
				state--;
			else if (buf[scan] == ']')
				state++;
			else if (state == 0 && strchr(rl_basic_word_break_characters, buf[scan - 1]) != NULL)
				break;
		}

		/* At this point, buf[scan] is either the start of a global
		 * name, or it is ".".  If "." we can't do anything with it.
		 */
		if (buf[scan] == '.')
			return NULL;
	} else {
		scan = start;
	}

	/* At this point, one way or another, buf[scan] is the start of a
	 * whole global name.  Look it up.
	 */
	next = strpbrk(&buf[scan], ".[");
	c = *next;
	*next = '\0';
	completion_container = json_context_by_key(context, &buf[scan]);
	*next = c;
	if (!completion_container)
		return NULL;

	/* Step through names or subscripts, until we get back to partial */
	for (;;) {
		/* skip '.' or subscript */
		if (c == '.')
			next++;
		else {
			for (state = 1, next++; state > 0; next++) {
				if (*next == '[')
					state++;
				else if (*next == ']')
					state--;
			}
			if (*next == '.') /* and it always should be */
				next++;
		}
		key = next;

		/* Find the end of this word.  If this is the last word,
		 * then do the completion thing.
		 */
		next = strpbrk(key, ".[");
		if (!next) {
			completion_key_offset = key - buf - start;
			return rl_completion_matches(text, member_name_generator);
		}
		c = *next;
		*next = '\0';

		/* Find this container */
		completion_container = json_by_key(completion_container, key);
		*next = c;

		/* Since we're skipping over subscripts, if this is an array
		 * then skip into its first element.  If no elements, then
		 * we can't do the completion.
		 */
		while (completion_container && completion_container->type == JSON_ARRAY)
			completion_container = completion_container->first;
		if (!completion_container)
			return NULL;
	}
}

/******************************************************************************/
static char *save_config()
{
	json_t	*config;
	char *tmp;
	static char	filename[200];
	FILE	*fp;

	/* Collect the config data into a JSON object */
	config = json_object();
	tmp = json_format_str(NULL);
	json_append(config, json_key("format", json_string(tmp, -1)));
	free(tmp);
	tmp = json_format_color_str();
	json_append(config, json_key("colors", json_string(tmp, -1)));
	free(tmp);
	if (autoload_dir)
		json_append(config, json_key("autoload", json_string(autoload_dir, -1)));

	/* Write the object to a file */
	sprintf(filename, "%s/.config/jsoncalc", getenv("HOME"));
	fp = fopen(filename, "w");
	if (!fp) {
		sprintf(filename, "%s/.jsoncalc", getenv("HOME"));
		fp = fopen(filename, "w");
		if (!fp) {
			perror(filename);
			json_free(config);
			return NULL;
		}
	}
	json_print(config, fp, NULL);
	fclose(fp);
	json_free(config);
	return filename;
}

/* Attempt to load jsoncalc's configuration from a file.  Ignore errors. */
static void load_config()
{
	char	filename[200];
	json_t	*config;
	char	*tmp;

	/* Figure out where the config file is located */
	sprintf(filename, "%s/.config/jsoncalc", getenv("HOME"));
	if (access(filename, R_OK | W_OK) != 0) {
		sprintf(filename, "%s/.jsoncalc", getenv("HOME"));
		if (access(filename, R_OK | W_OK) != 0)
			return;
	}

	/* Load it */
	config = json_parse_file(NULL, filename);
	if (!config)
		return;

	/* Extract info from it */
	tmp = json_text_by_key(config, "format");
	if (tmp)
		json_format(NULL, tmp);
	tmp = json_text_by_key(config, "colors");
	if (tmp)
		json_format_color(tmp);
	tmp = json_text_by_key(config, "autoload");
	if (tmp)
		autoload_dir = strdup(tmp);

	/* Free the config */
	json_free(config);
}

/******************************************************************************/

/* Interactively read a line from stdin.  This uses GNU readline unless it it
 * inhibitied or not configured.
 */
static char *jcreadline(const char *prompt)
{
	char	*expr;
	int	ch;
	size_t	i, size;

	/* Read a line into a dynamic buffer; */
	if (!inhibit_readline) {
		expr = readline("JsonCalc: ");
		if (!expr)
			return NULL;
		i = strlen(expr);
	} else {
		fputs(prompt, stdout);
		size = 80;
		expr = (char *)malloc(size);
		for (i = 0; (ch = getchar()) != EOF && ch != '\n'; i++) {
			if (size < i + 2) {
				size += 80;
				expr = realloc(expr, size);
			}
			expr[i] = ch;
		}
		if (ch == EOF) {
			free(expr);
			return NULL;
		}
	}

	/* Trim trailing whitespace, possibly including a newline */
	for (; i > 0 && isspace(expr[i - 1]); i--) {
	}
	expr[i] = '\0';

	/* Add to history, if using readline */
	if (!inhibit_readline) {
		add_history(expr);
		write_history(HISTORY_FILE);
	}

	/* Return it */
	return expr;
}

/******************************************************************************/

/* Load the contents of a file into a dynamically-allocated string buffer */
char *jcreadscript(const char *filename)
{
	FILE	*fp;
	long	size;
	char	*buf;

	/* Open the file, or give an error message */
	fp = fopen(filename, "r");
	if (!fp) {
		perror(filename);
		return NULL;
	}

	/* Find the length of the file */
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);

	/* Allocate space for the script, plus a terminating NULL */
	buf = (char *)malloc((size_t)(size + 1));

	/* Read it */
	fread(buf, 1, (size_t)size, fp);

	/* Add a terminating '\0' */
	buf[size] = '\0';

	/* Close the file */
	fclose(fp);

	/* Return it */
	return buf;
}

/******************************************************************************/
int main(int argc, char **argv)
{
	int i, len;
	jsoncalc_t *jc;
	json_t *jcthis, *result, *files, *autonames;
	char *val, *expr;
	int	saveconfig = 0;
	char    *tail, *err;
	int	opt;

	/* set the locale */
	val = setlocale(LC_ALL, "");
	if (json_debug_flags.calc)
		printf("Locale:  %s\n", val);

	/* Detect "--version" */
	if (argc >= 2 && !strcmp(argv[1], "--version")) {
		printf("jsoncalc %s\n", JCVERSION);
		printf("Copyright %s\n", JCCOPYRIGHT);
		puts("Freely redistributable under the terms of the");
		puts("GNU General Public License v3 or later.");
		return 0;
	}

	/* Start with default formatting */
	json_format(NULL, "");

	/* Try to load the config */
	load_config();

	/* Parse command-line flags */
	if (argc >= 2 && !strcmp(argv[1], "--help"))
		usage(NULL, NULL);
	expr = NULL;
	interactive = 0;
	while ((opt = getopt(argc, argv, "c:f:v:irl:uD:O:C:J:U")) >= 0) {
		switch (opt) {
		case 'c':
			if (expr)
				usage("You can only use one -ccalc or -fflag option\n", NULL);
			expr = strdup(optarg);
			break;
		case 'f':
			if (expr)
				usage("You can only use one -ccalc or -fflag option\n", NULL);
			expr = jcreadscript(optarg);
			if (!expr)
				exit(1);
			break;
		case 'i':
			interactive = 1;
			break;
		case 'r':
			inhibit_readline = 1;
			break;
		case 'v':
			envname = optarg;
			break;
		case 'l':
		case 'u':
			fprintf(stderr, "Not implemented yet\n");
			abort();
			break;
		case 'D':
			autoload_dir = optarg;
			break;
		case 'O':
			if (*optarg == '?' || (err = json_format(NULL, optarg)) != NULL) {
				format_usage();
				if (*optarg != '?') {
					puts(err);
					return 1;
				}
				return 0;
			}
			break;
		case 'C':
			if (*optarg == '?' || (err = json_format_color(optarg)) != NULL) {
				color_usage();
				if (*optarg != '?') {
					puts(err);
					return 1;
				}
				return 0;
			}
			break;
		case 'J':
			if (*optarg == '?' || json_debug(optarg)) {
				debug_usage();
				return 1;
			}
			break;
		case 'U':
			saveconfig = 1;
			break;
		default:
			{
				char optstr[2];
				optstr[0] = (char)opt;
				optstr[1] = '\0';
				usage("Invalid flag -%s\n", optstr);
			}
		}
	}

	/* Some sanity checks */
	if (expr && interactive) {
		usage("You can't mix -i with -ccalc or -ffile\n", NULL);
		return 1;
	}
	if (envname && interactive) {
		usage("You can-t mix -i with -venv\n", NULL);
		return 1;
	}

	/* Build an object from any parameters after the first */
	jcthis = json_object();
	context = NULL;
	files = NULL;
	for (i = optind; i < argc; i++) {
		json_t *tmp;

		/* If it ends with ".json", load it as a file */
		len = strlen(argv[i]);
		if (len > 5 && !strcmp(argv[i] + len - 4, ".json")) {
			tmp = json_parse_file(NULL, argv[i]);
			if (tmp) {
				tmp->next = files;
				files = tmp;
				context = json_context(context, tmp, NULL);
			}
		} else {
			/* In a name=value string, separate the name from the value */
			val = strchr(argv[i], '=');
			if (val)
				*val++ = '\0';
			else
				val = argv[i];

			/* Does the value look like JSON? */
			if (strchr("{[\"-.0123456789", *val)
			 || !strcmp("true", val)
			 || !strcmp("false", val)
			 || !strcmp("null", val))
				tmp = json_parse_string(val);
			else
				tmp = json_string(val, -1);
			json_append(jcthis, json_key(argv[i], tmp));
		}
	}

	/* Add the autoloader */
	autonames = json_object();
	context = json_context(context, autonames, autoload);

	/* Add the context from command-line args, if any */
	if (jcthis->first) {
		char *str = json_serialize(jcthis, 0);
		printf("\"this\": %s\n", str);
		free(str);
		context = json_context(context, jcthis, NULL);
	}

	if (expr) {
		/* Compile */
		jc = json_calc_parse(expr, &tail, &err);
		if (*tail)
			printf("%sTail:     %s%s\n", json_format_color_error, tail, json_format_color_end);
		if (err)
			printf("%sError:    %s%s\n", json_format_color_error, err, json_format_color_end);
		if (jc) {
			/* Evaluate */
			result = json_calc(jc, context, NULL);

			/* Print */
			if (json_print(result, stdout, NULL))
				putchar('\n');

			/* Clean up */
			json_calc_free(jc);
			json_free(result);
		}

		free(expr);
	} else {
		/* Enable the use of history and name completion while
		 * inputting expressions.
		 */
		using_history();
		read_history(HISTORY_FILE);
		rl_attempted_completion_function = jsoncalc_completion;
		rl_basic_word_break_characters = " \t\n\"\\'$><=;|&{}()[]#%^*+-:,/?~@";

		/* Read an expression */
		while ((expr = jcreadline("JsonCalc: ")) != NULL) {
			/* Ignore empty lines */
			if (!expr[0])
				continue;

			/* Maybe -Oformat? */
			if (!strncmp(expr, "-O", 2)) {
				for (val = expr + 2; *val == ' '; val++) {
				}
				if (*val == '?') {
					format_usage();
				} else {
					err = json_format(NULL, val);
					if (err)
						puts(err);
					val = json_format_str(NULL);
					printf("-O%s\n", val);
					free(val);
				}
				free(expr);
				continue;
			}

			/* Maybe -Ccolors? */
			if (!strncmp(expr, "-C", 2)) {
				for (val = expr + 2; *val == ' '; val++) {
				}
				if (*val == '?') {
					color_usage();
				} else {
					err = json_format_color(val);
					if (err)
						puts(err);
					val = json_format_color_str();
					printf("-C%s\n", val);
					free(val);
				}
				free(expr);
				continue;
			}

			/* Maybe -Jdebug? */
			if (!strncmp(expr, "-J", 2)) {
				for (val = expr + 2; *val == ' '; val++) {
				}
				err = *val ? json_debug(val) : "";
				if (err)
					debug_usage();
				free(expr);
				continue;
			}

			/* Compile */
			jc = json_calc_parse(expr, &tail, &err);
			if (*tail) {
				if (json_format_default.color)
					printf("%sTail:     %s%s\n", json_format_color_error, tail, json_format_color_end);
				else
					printf("Tail:     %s\n", tail);
			}
			if (err) {
				if (json_format_default.color)
					printf("%sError:    %s%s\n", json_format_color_error, err, json_format_color_end);
				else
					printf("Error:    %s\n", err);
			}
			free(expr);
			if (!jc)
				continue;

			if (json_debug_flags.calc) {
				/* Dump it. */
				printf("Parsed:  ");
				json_calc_dump(jc);
				putchar('\n');
			}

			/* Evaluate */
			result = json_calc(jc, context, NULL);

			/* Print the result */
			if (json_format_default.color && *json_format_color_result)
				fputs(json_format_color_result, stdout);
			if (json_print(result, stdout, NULL))
				putchar('\n');
			if (json_format_default.color && *json_format_color_result)
				fputs(json_format_color_end, stdout);

			/* Clean up */
			json_calc_free(jc);
			json_free(result);
		}

		/* Leave the cursor on the line after the last, unused prompt */
		putchar('\n');
	}

	/* If supposed to write the config, do that */
	if (saveconfig) {
		val = save_config();
		if (val)
			printf("Configuration written to %s\n", val);
	}

	/* Clean up & exit */
	while (context)
		context = json_context_free(context, 0);
	json_free(jcthis);
	json_free(autonames);
	history_truncate_file(HISTORY_FILE, 100);
	return 0;
}
