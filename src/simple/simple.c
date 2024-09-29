#include <stdio.h>
#include "json.h"
#include "calc.h"

int main(int argc, char **argv)
{
	jsoncmd_t *js;
	jsoncmdout_t *result;
	jsoncontext_t *context;

	context = json_context_std(NULL);
	js = json_cmd_parse_string(argv[1]);
	result = json_cmd_run(js, &context);
	if (result) {
		if (result->ret) {
			printf("returning ");
			json_print(result->ret, NULL);
			putchar('\n');
			json_free(result->ret);
		} else {
			printf("%s\n", result->text);
		}
	}
	json_cmd_free(js);
	json_context_free(context);
}

