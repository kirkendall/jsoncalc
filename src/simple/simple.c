#include <stdio.h>
#include <jx.h>

int main(int argc, char **argv)
{
	jxcmd_t *jc;
	jxcmdout_t *result;
	jxcontext_t *context;

	/* Create a context */
	context = jx_context_std(NULL);

	/* Parse the first command-line argument as a jx command */
	jc = jx_cmd_parse_string(argv[1]);
	if (jc != JX_CMD_ERROR) {
		/* Run the command */
		result = jx_cmd_run(jc, &context);

		/* If it returned anything, say what it returned */
		if (result) {
			if (result->ret) {
				/* Returned value */
				printf("returning ");
				jx_print(result->ret, NULL);
				putchar('\n');
				jx_free(result->ret);
			} else {
				/* Returned error */
				printf("%s\n", result->text);
			}
		}

		/* Clean up */
		jx_cmd_free(jc);
	}

	/* Free the context */
	jx_context_free(context);
	return 0;
}
