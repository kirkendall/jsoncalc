#include <stdio.h>
#include "json.h"
#include "calc.h"

int main(int argc, char **argv)
{
	jsoncmd_t *jc;
	jsoncmdout_t *result;
	jsoncontext_t *context;

	/* Create a context */
	context = json_context_std(NULL);

	/* Parse the first command-line argument as a JsonCalc command */
	jc = json_cmd_parse_string(argv[1]);
	if (jc != JSON_CMD_ERROR) {
		/* Run the command */
		result = json_cmd_run(jc, &context);

		/* If it returned anything, say what it returned */
		if (result) {
			if (result->ret) {
				/* Returned value */
				printf("returning ");
				json_print(result->ret, NULL);
				putchar('\n');
				json_free(result->ret);
			} else {
				/* Returned error */
				printf("%s\n", result->text);
			}
		}

		/* Clean up */
		json_cmd_free(jc);
	}

	/* Free the context */
	json_context_free(context);
	return 0;
}
