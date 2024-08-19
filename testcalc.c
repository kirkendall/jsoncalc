#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <locale.h>
#include "json.h"
#include "calc.h"

typedef struct {
	int	tests;
	int	failed;
} count_t;

int test_memleaks = 0;
int show_expression = 0;

/* Output a usage message and then exit */
void usage()
{
        puts("jsontest [flags] [file...]");
        puts("Flags: -m   Check for evidence of memory leaks during each test.");
        puts("       -e   Write each test to stderr before running it. Helps for core dumps.");
        puts("This reads a series of tests from a file, and writes any inconsistencies to");
        puts("stdout. Input lines starting with # are section headers. Lines starting with");
        puts("name= define data to be used in tests later in the file. Lines that don't");
        puts("start with #, name=, or = are tests. Lines starting with = are the expected");
        puts("result of the preceding test.");
        exit(0);
}

/* Evaluate a single test */
char *test(char *str, json_t *names)
{
        jsoncontext_t *context;
        jsoncalc_t *calc;
        json_t  *result;
        char    *resultstr, *tail, *err;
        int	before, compiled;  
        int	calcleaks, parseleaks;

        /* Compile it */
        before = json_debug_count;
        calc = json_calc_parse(str, &tail, &err);
        if (err)
                printf("Error: %s\n", err);
        if (!calc)
                return NULL;
	compiled = json_debug_count;

        /* Evaluate it */
        context = json_context(NULL, names, JSON_CONTEXT_NOFREE);
        result = json_calc(calc, context, NULL);
        resultstr = json_serialize(result, NULL);
        json_free(result);
        json_context_free(context);

        /* Memory leak in json_calc? */
        calcleaks = json_debug_count - compiled;
        if (test_memleaks && calcleaks != 0)
		printf("leak: %s (calc leaked %d json_t's)\n", str, calcleaks);

        /* Clean up.  Memory leak in json_calc_parse()? */
        json_calc_free(calc);
        parseleaks = json_debug_count - before - calcleaks;
        if (test_memleaks && parseleaks != 0)
		printf("leak: %s (parser allocated %d leaked %d)\n", str, compiled - before, parseleaks);

        /* Return the result */
        return resultstr;
}

/* Read a series of tests, and run them */
void testfile(FILE *in, count_t *counts)
{
        char    buf[1000];
        char	section[1000];
	char	expression[1000];
	char	*resultstr = NULL;
        char    *tmp;
        json_t  *names;

        /* Create an object for storing defined names */
        names = json_object();

        /* For each line ... */
        while (fgets(buf, sizeof buf, in)) {
                /* Strip off the newline */
                tmp = strchr(buf, '\n');
                if (tmp)
                        *tmp = '\0';

		/* Skip blank lines */
		if (!buf[0])
			continue;

                /* Remember # lines as section labels */
                if (buf[0] == '#') {
			strcpy(section, buf);
			continue;
		}

		/* If result line, and we have a previous result, check it */
		if (buf[0] == '=' && resultstr) {
			if (strcmp(resultstr, buf + 1)) {
				if (*section) 
					puts(section);
				printf("expr: %s\n", expression);
				printf("want: %s\n", buf + 1);
				printf("got:  %s\n", resultstr);
				counts->failed++;
			}
			continue;
		}

                /* If name=value then parse it and store it */
                if (isalpha(buf[0])) {
                        for (tmp = buf; isalnum(*tmp); tmp++) {
                        }
                        if (*tmp == '=' && tmp[1] != '=') {
                                *tmp++ = '\0';
                                json_t *value = json_parse_string(tmp);
                                if (value)
                                        json_append(names, json_key(buf, value));
                                continue;
                        }
                }

                /* Anything else is a test.  Evaluate it */
                if (show_expression) {
			fputs(buf, stderr);
			putc('\n', stderr);
		}
		strcpy(expression, buf);
		if (resultstr)
			free(resultstr);
                resultstr = test(buf, names);
                counts->tests++;
        }

        /* Clean up the names */
        json_free(names);
}


int main(int argc, char **argv)
{
	int ch;
	count_t counts = {0,0};
        setlocale(LC_ALL, "");

        /* Use ASCII output, because it's trickier than UTF-8 */
        json_format(NULL, "ascii");

        /* Parse command-line flags */
        while ((ch = getopt(argc, argv, "me")) >= 0)
        {
		switch (ch) {
		  case 'm': test_memleaks = 1;	break;
		  case 'e': show_expression = 1;break;
		  default:
			usage();
		}
        }

	if (optind == argc) {
		testfile(stdin, &counts);
	} else {
		FILE *fp;

		for (; optind < argc; optind++) {
			fp = fopen(argv[optind], "r");
			if (fp) {
				testfile(fp, &counts);
				fclose(fp);
			} else {
				perror(argv[optind]);
			}
		}
	}

	printf("Passed %d/%d, failed %d/%d\n", counts.tests - counts.failed, counts.tests, counts.failed, counts.tests);
        return counts.failed > 0;
}

