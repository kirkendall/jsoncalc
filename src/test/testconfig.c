#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <locale.h>
#include <jx.h>

/* Run a single test */
static int singletest(char *settings)
{
	jx_t	*err;

	err = jx_config_parse(NULL, settings, 0);
	if (!err) {
		jx_print(jx_config, NULL);
		return 1;
	}
	if (isatty(0))
		printf("\e[31m%s\e[m\n", err->text);
	else {
		puts(err->text);
	}
	jx_free(err);
	return 0;
}

int main(int argc, char **argv)
{
	int	i;
	char	buf[100], *eol;
	jx_t	*dummy;

	setlocale(LC_ALL,"");
	jx_config_load("textconfig");

	/* Add a dummy plugin */
	dummy = jx_object();
	jx_append(dummy, jx_key("host", jx_string("localhost", -1)));
	jx_append(dummy, jx_key("db", jx_string("", -1)));
	jx_append(dummy, jx_key("user", jx_string("", -1)));
	jx_append(jx_by_key(jx_config, "plugin"), jx_key("dummy",dummy));

	jx_print(jx_config, NULL);
	if (argc <= 1) {
		for (;;) {
			if (isatty(0))
				printf("testconfig> ");
			if (!fgets(buf, sizeof buf, stdin))
				break;
			eol = strchr(buf, '\n');
			if (eol)
				*eol = '\0';
			singletest(buf);
		}
	}
	for (i = 1; i < argc; i++)
		singletest(argv[i]);
	return 0;
}
