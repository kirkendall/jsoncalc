#include <stdlib.h>
#include <stdio.h>
#include <jsoncalc.h>


/* This is a helper function for json_walk().  The big difference between this
 * and json_walk() is that this function checks siblings, but json_walk()
 * does not.
 */
static int jcwalk(json_t *json, int (*callback)(json_t *, void *), void *data)
{
	int	quit;

	/* We'll be iterating over the ->next pointers */
	while (json != NULL) {
		/* Call the callback for this node */
		quit = (*callback)(json, data);
		if (quit != 0)
			return quit;

		/* If ->first is non-NULL, walk there too (recursively) */
		if (json->type != JSON_NULL && json->first) { /* undeferred */
			quit = json_walk(json->first, callback, data);
			if (quit != 0)
				return quit;
		}

		/* Move to the next node */
		json = json->next; /* undeferred */
	}

	/* If we get here, then we didn't quit before the end */
	return 0;
}

/* Visit each node in a json_t tree, calling a callback function for each one.
 * 
 * The nodes are visited in branch-first order, which is roughly the order
 * in which they'd be printed if converted back to JSON text.
 *
 * The callback function is passed the node, and a (void*) that you can use
 * to pass counters or whatever your callback needs.  The callback should
 * return 0 normally, or some other value to stop walking immediately.
 *
 * json_walk() itself returns 0 normally, or the value returned by the
 * callback if the walk was cut short.
 */
int json_walk(json_t *json, int (*callback)(json_t *, void *), void *data)
{
	int quit;

	/* Defend against NULL */
	if (!json)
		return 0;

	/* If applied to a deferred array, convert it to undeferred */
	json_undefer(json);

	/* Call the function for this node */
	quit = (*callback)(json, data);
	if (quit != 0)
		return quit;

	/* For JSON_NULL, don't recurse. */
	if (json->type == JSON_NULL)
		return 0;

	/* Do ->first and all of its children and siblings */
	return jcwalk(json, callback, data);
}
