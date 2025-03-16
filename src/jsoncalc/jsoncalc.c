#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <glob.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include "json.h"
#include "calc.h"
#include "jsoncalc.h"
#include "version.h"



/* getopt() variables not declared in <unistd.h> */
extern char *optarg;
extern int optind, opterr, optopt;

/* -i -- Force interactive mode */
int interactive = 0;

/* -ccmd or -ffile -- Suggest batch mode if not forced interactive */
int maybe_batch = 0;

/* -r -- Inhibit the readline library? */
int inhibit_readline = 0;

/* -s -- Safer -- Limit file access to named files, and inhibit shell access */
int safer = 0;

/* -u -- Update -- Allow files named on the command line to be rewritten */
int allow_update = 0;

/* -D -- This is a directory to check for autoload */
char *autoload_dir = NULL;

/* This is the context -- a stack of data that is available to expressions. */
jsoncontext_t *context;

/* This is the list of commands to execute for each data file.  This is defined
 * via -ccmd or -ffile flags.
 */
jsoncmd_t *initcmd = NULL;

/* Output a usage message for the -Oformat option */
void format_usage()
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
	printf("  -O%-11sFor simple strings, output them without quoting.\n", json_format_default.string ? "string" : "nostring");
	printf("  -O%-11sFor simple nulls, if the null represents an error then show it.\n", json_format_default.error ? "error" : "noerror");
	printf("  -O%-11sAdd color for ANSI terminals.\n", json_format_default.color ? "color" : "nocolor");
	printf("  -Odigits=%-4dPrecision when converting numbers to strings.\n", json_format_default.digits);
}

/* Output a color usage statement */
void color_usage()
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
void debug_usage()
{
	puts("The -Jflags option controls debugging flags.  The flags are single letters");
	puts("indicating what should be debugged.  The flag letters are:");
	puts("  a  Call abort() when an error is detected.");
	puts("  e  Output info about json_by_expr() calls.");
	puts("  c  Output info about json_calc() calls.");
	puts("  t  Trace each command as it is run.");
	puts("");
	puts("You may also put a + or - or = between -J and the flags to alter the way the new");
	puts("flags are combined with existing flags.  The means are:");
	puts("  +  Add the new flags to existing flags. (This is the default.)");
	puts("  =  Turn off all flags except the new flags.");
	puts("  -  Turn off existing flags that are also in new flags.");
	printf("\nCurrent flags are: -J%s%s%s%s\n",
		json_debug_flags.abort ? "a" : "",
		json_debug_flags.expr ? "e" : "",
		json_debug_flags.calc ? "c" : "",
		json_debug_flags.trace ? "t" : "");
}

/* Output a usage message and then exit. */
static void usage(char *fmt, char *data)
{
	puts("Usage: jsoncalc [flags] [name=value]... [file.json]...");
	puts("Flags: -c calc    Evaluate calc, output any results, and quit.");
	puts("       -f file    Read calc expression from file, output the result, and quit.");
	puts("       -i         Interactive.");
	puts("       -r         Inhibit the use of readline for interactive input.");
	puts("       -u         Update - Write back any modified *.json files listed in args.");
	puts("       -s         Safer - Limit shell and file access for security.");
	puts("       -o         Object - Assume new files should contain an empty object.");
	puts("       -a         Array - Assume new files should contain an empty array.");
	puts("       -l plugin  Load libjcplugin.so from $JSONCALCPATH or $LIBPATH.");
	puts("       -d dir     Autoload files from directory \"dir\" (this invocaion only).");
	puts("       -D dir     Autoload files from directory \"dir\" (persist, via -U).");
	puts("       -O format  Adjusts the output format. Use -O? for more info.");
	puts("       -C colors  Adjusts colors for ANSI terminals. Use -C? for more info.");
	puts("       -J flags   Debug: t=token, f=file, b=buffer, a=abort, e=expr, c=calc");
	puts("       -U         Save -O, -C, -D, -J flags as default config");
	puts("This program manipulates JSON data. Without one of -ccalc or -ffile,");
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

		sprintf(filename, "%s/*.json", autoload_dir);
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
	sprintf(filename, "%s/%s.json", autoload_dir, key);
	if (access(filename, R_OK) != 0)
		return NULL;

	/* Try to parse the file */
	json = json_parse_file(filename);
	return json;
}


/* This is a context hook.  It adds a layer a the standard context for
 * autoloading files from the -Ddirectory.
 */
static jsoncontext_t *autodir(jsoncontext_t *context)
{
	/* Add the autoloader */
	context = json_context(context, json_object(), JSON_CONTEXT_GLOBAL);
	context->autoload = autoload;
	return context;
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

void run(jsoncmd_t *jc, jsoncontext_t **refcontext)
{
	jsoncmdout_t *result;

	result = json_cmd_run(jc, &context);
	if (result) {
		if (result->ret == &json_cmd_break)
			puts("RETURNED A \"BREAK\"");
		else if (result->ret == &json_cmd_continue)
			puts("RETURNED A \"CONTINUE\"");
		else if (result->ret) {
			printf("RETURNED A VALUE: ");
			json_print(result->ret, NULL);
			json_free(result->ret);
		} else {
			fputs(json_format_color_error, stderr);
			if (result->filename)
				fprintf(stderr, "%s:%d: %s\n", result->filename, result->lineno, result->text);
			else if (result->lineno)
				fprintf(stderr, "Line %d: %s\n", result->lineno, result->text);
			else
				fprintf(stderr, "%s\n", result->text);
			fputs(json_format_color_end, stderr);
			putc('\n', stderr);
		}
		free(result);
	}
}

/* Try to guess whether a given file is a data file based on its name and
 * maybe its contents.
 */
int isscript(char *filename)
{
	char	*ext = strrchr(filename, '.');
	char	buf[100], *s;
	FILE	*fp;
	size_t	nbytes;

	/* If it has a well known filename extension, trust it */
	if (ext) {
		if (!strcasecmp(ext, ".json")
		 || !strcasecmp(ext, ".xml")
		 || !strcasecmp(ext, ".wsdl")
		 || !strcasecmp(ext, ".csv"))
			return 0;
		if (!strcasecmp(ext, ".jc")
		 || !strcasecmp(ext, ".jsoncalc")
		 || !strcasecmp(ext, ".js")) /* I know, but it isn't data */
			return 1;
	}

	/* Okay, we'll do it the hard way: Check the first few bytes */
	fp = fopen(filename, "r");
	if (fp) {
		nbytes = fread(buf, 1, sizeof buf - 1, fp);
		fclose(fp);
		if (nbytes < 2)
			return 0; /* too short to be a meaningful script */
		buf[nbytes] = '\0';
		if (!strncmp(buf, "#!", 2) || !strncmp(buf, "//", 2))
			return 1; /* Looks script-ish! */

		/* One last chance for a script: If it starts with a word
		 * followed by a character other than comma, it's a script.
		 */
		if (isalpha(buf[0])) {
			for (s = buf; isalnum(*s); s++) {
			}
			if (*s != ',' && *s)
				return 1;
		}
	}

	/* Okay, we can't say for sure.  Assume data */
	return 0;
}


/******************************************************************************/
int main(int argc, char **argv)
{
	int	i;
	json_t	*args;
	char	*val;
	int	saveconfig = 0;
	char	*errmsg;
	int	anyformat = 0;
	int	anyfiles = 0;
	int	opt;
	int	exitcode = 0;

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

	/* Create an object that will old any name=value parameters, and
	 * start a context.
	 */
	args = json_object();
	context = json_context_std(args);

	/* Parse command-line flags */
	if (argc >= 2 && !strcmp(argv[1], "--help")) {
		while (context)
			context = json_context_free(context);
		usage(NULL, NULL);
	}
	interactive = 0;
	while ((opt = getopt(argc, argv, "c:f:irsoal:uD:O:C:J:U")) >= 0) {
		switch (opt) {
		case 'c':
			initcmd = json_cmd_append(initcmd, json_cmd_parse_string(optarg), context);
			maybe_batch = 1;
			break;
		case 'f':
			initcmd = json_cmd_append(initcmd, json_cmd_parse_file(optarg), context);
			maybe_batch = 1;
			break;
		case 'i':
			interactive = 1;
			break;
		case 'r':
			inhibit_readline = 1;
			break;
		case 's':
			safer = 1;
			break;
		case 'o':
			json_file_new_type = 'o';
			break;
		case 'a':
			json_file_new_type = 'a';
			break;
		case 'l':
			fprintf(stderr, "Not implemented yet\n");
			abort();
			break;
		case 'u':
			allow_update = 1;
			break;
		case 'D':
			autoload_dir = optarg;
			break;
		case 'O':
			if (*optarg == '?' || (errmsg = json_format(NULL, optarg)) != NULL) {
				while (context)
					context = json_context_free(context);
				format_usage();
				if (*optarg != '?') {
					puts(errmsg);
					return 1;
				}
				return 0;
			}
			anyformat = 1;
			break;
		case 'C':
			if (*optarg == '?' || (errmsg = json_format_color(optarg)) != NULL) {
				while (context)
					context = json_context_free(context);
				color_usage();
				if (*optarg != '?') {
					puts(errmsg);
					return 1;
				}
				return 0;
			}
			break;
		case 'J':
			if (*optarg == '?' || json_debug(optarg)) {
				while (context)
					context = json_context_free(context);
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
				while (context)
					context = json_context_free(context);
				usage("Invalid flag -%s\n", optstr);
			}
		}
	}

	/* Register the -Ddirectory autoload.  However, since we have already
	 * started the context (so -ccalc and -ffile could set variables),
	 * we must also explicitly insert it into JsonCalc's main context.
	 */
	if (autoload_dir) {
		/* Add the hook, for the benefit of any later contexts */
		json_context_hook(autodir);

		/* Too late to automatically add it to JsonCalc's main context,
		 * so we explicitly add it just under the "this" context.
		 */
		jsoncontext_t *thiscontext = json_context_insert(&context, JSON_CONTEXT_THIS);
		thiscontext->older = autodir(thiscontext->older); 
	}

	/* If no "-i" was given, then the first argument after options may be
	 * the name of a script to load and run like "-f".  Or it could be the
	 * first data file.  Check!
	 * bytes to see if it looks script-ish.
	 */
	if (!interactive && optind < argc && isscript(argv[optind])) {
		initcmd = json_cmd_append(initcmd, json_cmd_parse_file(argv[optind]), context);
		maybe_batch = 1;
		optind++;
	}

	/* Build an object from any name=value parameters */
	for (i = optind; i < argc; i++) {
		json_t *tmp;

		/* If it looks like a file, load it as a file */
		if (strchr(argv[i], '.')
		 || !strcmp(argv[i], "-")
		 || 0 == access(argv[i], F_OK)) {
			/* If it doesn't exist and no -a/-o was given, fail */
			if (strcmp(argv[i], "-") && 0 != access(argv[i], F_OK) && json_file_new_type == '\0'){
				perror(argv[i]);
				exitcode = 1;
				goto CleanExit;
			}
			json_context_file(context, argv[i], allow_update, NULL);
			anyfiles = 1;
		} else {
			/* In a name=value string, separate the name from the value */
			val = strchr(argv[i], '=');
			if (val)
				*val++ = '\0';
			else
				val = "true";

			/* Does the value look like JSON? */
			if (strchr("{[\"-.0123456789", *val) /*}*/
			 || !strcmp("true", val)
			 || !strcmp("false", val)
			 || !strcmp("null", val))
				tmp = json_parse_string(val);
			else
				tmp = json_string(val, -1);
			json_append(args, json_key(argv[i], tmp));
		}
	}

	/* Start on the first file named on the command line, if any */
	json_context_file(context, NULL, 0, NULL);

	/* If -ccmd or -ffile resulted in an error (already reported) then quit
	 */
	if (initcmd == JSON_CMD_ERROR) {
		while (context)
			context = json_context_free(context);
		exit(2);
	}

	/* If no commands were defined via -ccmd or -ffile, and no -i was given
	 * to explicitly make this be interactive, but a -Oformat was used to
	 * specify an output format, then assume -cthis so any data will be
	 * output in the requested format.
	 */
	if (!initcmd && !interactive && anyformat)
		initcmd = json_cmd_parse_string("this");

	/* If still no initcmd then assume interactive even without -i */
	if (!initcmd)
		interactive = 1;

	/* If batch mode and no filenames were named on the command line,
	 * then assume "-".
	 */
	if (!interactive && !anyfiles)
		json_context_file(context, "-", 1, NULL);

	/* Start on the first file */
	json_context_file(context, NULL, 0, NULL);

	/* Do either the batch or interactive thing */
	if (interactive)
		interact(&context, initcmd);
	else
		batch(&context, initcmd);

	/* If supposed to write the config, do that */
	if (saveconfig) {
		val = save_config();
		if (val)
			printf("Configuration written to %s\n", val);
	}

	/* Clean up & exit */
CleanExit:
	if (allow_update) {
		/* Switch to previous file to trigger update, if the current
		 * file was modified.  Switching to the previous file is enough
		 * to trigger this even if there was no previous file.
		 */
		i = JSON_CONTEXT_FILE_PREVIOUS;
		json_context_file(context, NULL, 0, &i);
	}
	while (context)
		context = json_context_free(context);
	json_cmd_free(initcmd);
	return exitcode;
}
