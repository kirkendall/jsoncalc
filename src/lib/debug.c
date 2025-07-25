#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jsoncalc.h>

/* Debugging flags.  These are globally accessible, but normally you'd only
 * wnt to change them via the json_debug() function.
 */
json_debug_t json_debug_flags;

/* Control debugging output.  Returns NULL if successful, or a pointer to a
 * bad flag within "flags" if failure.  The "flags" string can contain "+"
 * to turn flags on, "-" to turn them off, "=" to turn off all flags except
 * those that follow "=", or a letter for a specific feature to debug.
 * The features are "t" trace commands as they're executed.
 *                  "a" to call abort() on a JSON error.
 *                  "e" to output info for json_by_expr()
 *                  "c" to output info for json_calc()
 */
char *json_debug(char *flags)
{
	int	set = 1;
	while (*flags)
	{
		switch (*flags++)
		{
		  case 'a':	json_debug_flags.abort = set;	break;
		  case 'e':     json_debug_flags.expr = set;	break;
		  case 'c':     json_debug_flags.calc = set;	break;
		  case 't':	json_debug_flags.trace = set;	break;
		  case '+':	set = 1;			break;
		  case '-':	set = 0;			break;
		  case '=':
		  	memset(&json_debug_flags, 0, sizeof json_debug_flags);
		  	set = 1;
		  	break;
		  default:
			return flags;
		}
	}
	return NULL;
}
