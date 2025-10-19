/* Memory.c
 *
 * This file contains a variety of low-level json_t allocation functions,
 * and the json_free() function for deallocating them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <jsoncalc.h>

/* Here we need to access the "real" allocation/free functions */
#ifdef JSON_DEBUG_MEMORY
# undef json_free
# undef json_simple
# undef json_string
# undef json_number
# undef json_from_int
# undef json_from_double
# undef json_boolean
# undef json_null
# undef json_error_null
# undef json_array
# undef json_key
# undef json_object
# undef json_defer
# undef json_first
# undef json_parse_string
# undef json_copy
# undef json_copy_filter
# undef json_calc
#endif

/* For debugging, this is used to store places that allocate memory, and
 * how many nodes were allocated there without freeing.  If a program links
 * with this library without defining JSON_DEBUG_MEMORY then this will be
 * unused, but it's fairly small.
 */
typedef struct
{
        const char *file;
        int  line;
        int  count;
} memory_tracker_t;
static memory_tracker_t *memory_tracker;

/* This counts the number of json_t's currently allocated.  Not threadsafe! */
int json_debug_count = 0;

/* Return an estimated byte count for a given json_t tree */
size_t json_sizeof(json_t *json)
{
        size_t size = 0;
        size_t len;

        while (json) {
                size += sizeof(json_t);

                switch (json->type) {
                  case JSON_STRING:
                  case JSON_NUMBER:
                  case JSON_BOOLEAN:
                  case JSON_NULL:
                  case JSON_KEY:
                        /* Add the text length including the terminating \0,
                         * tweaked for alignment
                         */
                        len = ((strlen(json->text) - sizeof(json->text)) | 0x1f) + 1;
                        size += len;
                        break;

                  case JSON_DEFER:
			/* Rough guess: twice the size of a json_t.  We count
			 * one json_t after this switch, so we just add 1 here.
			 */
			size += sizeof(json_t);
			break;

                  case JSON_ARRAY:
                  case JSON_OBJECT:
                  case JSON_BADTOKEN:
                  case JSON_NEWLINE:
                  case JSON_ENDARRAY:
                  case JSON_ENDOBJECT:
                        ; /* Listed just to keep the compiler happy */
                }

                /* Recursively add the "first" data */
                size += json_sizeof(json->first);

                /* Iteratively add the "next" data */
                json = json->next; /* undeferred */
        }

        return size;
}

/* Free a JSON data tree */
void json_free(json_t *json)
{
        json_t *next;

	/* Defend against NULL */
	if (!json)
		return;

        /* If this was allocated with memory debugging turned on, but freed
         * where it isn't turned on, then complain.
         */
	assert(json->memslot == 0 || memory_tracker[json->memslot].line != 0);

	/* If ->next points to a JSON_DEFER, that means this json_t is an
	 * element of a deferred array, currently being scanned.  Free the
	 * resources used for this scan session.
	 */
	if (json->next && json->next->type == JSON_DEFER) {
		jsondeffns_t *fns = (jsondeffns_t *)json->next;
		if (fns->free)
			(*fns->free)(json);
	}

	/* Iteratively free this node and its siblings */
	while (json) {

		/* If this is a JSON_DEFER array, then free its resources
		 * specially
		 */
		if (json->first && json->first->type == JSON_DEFER) {
			jsondeffns_t *fns = (jsondeffns_t *)json->next;
			if (fns->free)
				(*fns->free)(json);
		}

		/* Recursively free contained data.  Note that JSON_NULL nodes
		 * abuse the ->first field to store a pointer into source text
		 * instead of a json_t.
		 */
		if (json->type != JSON_NULL)
			json_free(json->first);

		/* Free this json_t struct */
		next = json->next; /* undeferred */
		free(json);
		json_debug_count--;
		json = next;
	}
}

/* Allocate a json_t node and initialize some fields */
json_t *json_simple(const char *str, size_t len, jsontype_t type)
{
	json_t *json;
	size_t  size;

	/* String is optional.  If omitted then use "" as a placeholder */
	if (!str)
		len = 0;
	else if (len == (size_t)-1)
		len = strlen(str);

        /* Compute the size, rounding up to a multiple of 32 */
        size = sizeof(json_t) - sizeof json->text + len + 1;
        size = (size | 0x1f) + 1;

	/* Allocate it.  Trust malloc() to be efficient */
        json = (json_t *)malloc(size);

	/* Initialize the fields */
	memset(json, 0, size);
	json->type = type;
	if (str)
		strncpy(json->text, str, len);

	/* return it */
	json_debug_count++;
	return json;
}

/* Allocate a json_t for a given string.  Note that any escape sequences
 * such as \n or \u22c8 are handled by the parser via json_mbs_unescape()
 * so we just get the actual data here.  Passing -1 for len causes it to
 * compute the length via strlen().  "str" is not required to have a
 * terminating '\0' but the returned json->text field will.
 */
json_t *json_string(const char *str, size_t len)
{
	return json_simple(str, len, JSON_STRING);
}

/* Allocate a json_t for a given number, expressed as a string.  If you pass
 * -1 for len, it'll compute the length via strlen().
 */
json_t *json_number(const char *str, size_t len)
{
	return json_simple(str, len, JSON_NUMBER);
}

/* Allocate a json_t for a given integer */
json_t *json_from_int(int i)
{
	json_t *json = json_number("", 0);
	json->text[1] = 'i';
	JSON_INT(json) = i;
	return json;
}

/* Allocate a json_t for a given floating-point number */
json_t *json_from_double(double f)
{
	json_t *json = json_number("", 0);
	json->text[1] = 'd';
	JSON_DOUBLE(json) = f;
	return json;
}

/* Allocate a json_t for a boolean value. */
json_t *json_boolean(int boolean)
{
	if (boolean)
		return json_simple("true", 4, JSON_BOOLEAN);
	else
		return json_simple("false", 5, JSON_BOOLEAN);
}

/* Allocate a json_t for a null value */
json_t *json_null(void)
{
	return json_simple("", 0, JSON_NULL);
}

/* Allocate a json_t for a null value, encoding an error message */
json_t *json_error_null(const char *where, const char *fmt, ...)
{
	char	buf[200], *bigbuf;
	int	len;
	va_list	ap;
	json_t	*result;

	/* First try it in a modest buffer.  Usually works. */
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (len < 0)
		return json_null();
	if (len <= sizeof buf - 1) {
		result = json_simple(buf, len, JSON_NULL);
		result->first = (json_t *)where;
	}

	/* Allocate a larger buffer to hold the string, and use it */
	bigbuf = (char *)malloc(len);
	va_start(ap, fmt);
	vsnprintf(bigbuf, len, fmt, ap);
	va_end(ap);
	result = json_simple(buf, len, JSON_NULL);
	free(bigbuf);
	result->first = (json_t *)where;
	return result;
}

/* Allocate a json_t for a key.  If value is non-NULL, then it will be used
 * as the value associated with the key.  Later, you can use json_append()
 * to assign a value to it too.
 */
json_t *json_key(const char *key, json_t *value)
{
	/* Allocate it with twice as much space for storing the key's name.
	 * This is so we can also store the simplified version later, if
	 * necessary.
	 */
	json_t *json = json_simple(key, strlen(key) * 2 + 1, JSON_KEY);
	json->first = value;
	return json;
}

/* Allocate a json_t for an empty object */
json_t *json_object()
{
	return json_simple(NULL, 0, JSON_OBJECT);
}

/* Allocate a json_t for an empty array */
json_t *json_array()
{
	return json_simple(NULL, 0, JSON_ARRAY);
}

/* This is a dummy type of deferred array which is always empty.  It is used
 * when json_defer() is called with NULL instead of a real list of deferred
 * functions.
 */
static json_t *dummy(json_t *node)
{
	return NULL;
};
static jsondeffns_t dummyfns = {
	sizeof(json_t) + sizeof(jsondeffns_t),	/* size (of the whole more-than-json_t */
	"dummy",	/* desc */
	dummy,		/* first */
	dummy,		/* next */
	NULL,		/* islast */
	NULL,		/* free */
	NULL,		/* byindex */
	NULL		/* bykey */
};

/* Allocate a JSON_DEFER node.  "fns" is a collection of function pointers
 * that implement the desired type of deferred array, or NULL to use a
 * dummy set of functions.
 */
json_t *json_defer(jsondeffns_t *fns)
{
	json_t *json;
	size_t	size;

	/* If no "fns" then use dummy */
	if (!fns)
		fns = &dummyfns;

	/* Allocate it, with extra space.  Note that we must tweak the size
	 * because json_simple wants to be passed the size of the "text" field,
	 * but fns->size is the size of the whole thing.
	 */
	size = fns->size - sizeof(json_t) + sizeof json->text;
	json = json_simple(NULL, size, JSON_DEFER);

	/* Store the fns pointer, with this deferred array's implementation
	 * functions.  The rest of the jsondef_t is already initialized to 0's
	 */
	((jsondef_t *)json)->fns = fns; 

	/* Return it */
	return json;
}

/******************************************************************************/

/* For debugging memory issues, this function is called when the program exits
 * to check for memory leaks.
 */
static void memory_check_leaks(void)
{
        int     i;
        if (!memory_tracker)
		return;
#ifdef JSON_DEBUG_MEMORY
	json_debug_free(__FILE__, __LINE__, json_system);
#endif
        for (i = 0; i < 4096; i++)
                if (memory_tracker[i].count > 0)
                        fprintf(stderr, "%s:%d: Leaked %d json_t's\n", memory_tracker[i].file, memory_tracker[i].line, memory_tracker[i].count);
        if (memory_tracker[4096].count > 0)
                fprintf(stderr, "Leaked %d json_t's from an untracked source\n", memory_tracker[4096].count);
}

/* For debugging, this looks for a slot for counting allocations from a given
 * source line.
 */
static int memory_slot(const char *file, int line)
{
        int     slot, start;

        /* If memory tracking hasn't been initialized, then do so now */
        if (!memory_tracker) {
                /* Allocate memory for the tracker.  Each json_t has a 12-bit
                 * field for tracking its allocation source, so we want 4096
                 * tracker slots.  We also want one more slot in case there
                 * are more than 4096 source lines that allocate json_t's.
                 */
                memory_tracker = (memory_tracker_t *)calloc(4097, sizeof(memory_tracker_t));

                /* Arrange for memory leaks to be reported at exit */
                atexit(memory_check_leaks);
        }

        /* Choose a slot for this source line's counter */
        start = slot = abs(line) % 4096;
        if (slot == 0)
                slot++; /* slot 0 is reserved for uncounted allocations */
        do {
                /* Found an empty slot */
                if (!memory_tracker[slot].file) {
                        memory_tracker[slot].file = file;
                        memory_tracker[slot].line = line;
/*printf("%s:%d: allocating slot %d\n", file, line, slot);*/
                        return slot;
                }

                /* Found an existing slot for this file/line */
                if (memory_tracker[slot].file == file && memory_tracker[slot].line == line) {
/*printf("%s:%d: reusing slot %d\n", file, line, slot);*/
                        return slot;
		}

                /* Bumped to next slot */
                slot = (slot & 0xfff) + 1;
        } while (slot != start);

        /* If we get here then we looped without ever finding the slot or an
         * empty slot.  All we can do is count it in the overflow area.
         */
/*printf("%s:%d: forced to use slot 0\n", file, line);*/
        return 0;
}


/* The following are debugging wrappers around the above functions.  They are
 * normally only called if the source program defines JSON_DEBUG_MEMORY but
 * we want to use the same library whether debugging is being used or not,
 * so these are defined unconditionally.
 */
void json_debug_free(const char *file, int line, json_t *json)
{
	json_t *next;

        /* Iterate over the ->next links */
        for (; json; json = next)
        {
		next = json->next; /* undeferred */

		/* If it looks like it was already freed, then complain.
		 * This isn't reliable!  It won't reject valid free's but it
		 * might miss some invalid ones, if the memory was recycled.
		 */
		if (json->type == JSON_BADTOKEN) {
			fprintf(stderr, "%s:%d: Attempt to free memory twice\n", file, line);
			if (json->memslot)
				fprintf(stderr, "%s:%d: This is where it was first freed\n", memory_tracker[json->memslot].file, -memory_tracker[json->memslot].line);
		}

		/* We track by where the json_t is allocated not by where it
		 * is freed.  Decrement the allocation count for that line.
		 */
		int slot = json->memslot;
		if (slot != 0 && memory_tracker[slot].count == 0 ) {
			fprintf(stderr, "%s:%d: Attempt to re-free memory allocated at %s:%d (slot %d)\n", file, line, memory_tracker[slot].file, memory_tracker[slot].line, slot);
			abort();
		}
		else if (memory_tracker)
			memory_tracker[slot].count--;

		/* Free the ->first link recursively... except that an error
		 * "null" uses ->first for the position of the error, so we
		 * don't want to free that.
		 */
		if (!json_is_error(json))
			json_debug_free(file, line, json->first);

		/* Free this node */
		json->first = json->next = NULL; /* undeferred */
		json->memslot = memory_slot(file, -line);
		json_free(json);
	}
}

json_t *json_debug_simple(const char *file, int line, const char *str, size_t len, jsontype_t type)
{
        json_t  *json;
        int slot = memory_slot(file, line);
        memory_tracker[slot].count++;
        json = json_simple(str, len, type);
        json->memslot = slot;
        return json;
}

/* Allocate a json_t for a given string. */
json_t *json_debug_string(const char *file, int line, const char *str, size_t len)
{
        return json_debug_simple(file, line, str, len, JSON_STRING);
}

/* Allocate a json_t for a given number, expressed as a string */
json_t *json_debug_number(const char *file, int line, const char *str, size_t len)
{
	return json_debug_simple(file, line, str, len, JSON_NUMBER);
}

/* Allocate a json_t for a given integer */
json_t *json_debug_from_int(const char *file, int line, int i)
{
	json_t	*json = json_debug_number(file, line, "", 0);
	json->text[1] = 'i';
	JSON_INT(json) = i;
	return json;
}

/* Allocate a json_t for a given floating-point number */
json_t *json_debug_from_double(const char *file, int line, double f)
{
	json_t	*json = json_debug_number(file, line, "", 0);
	json->text[1] = 'd';
	JSON_DOUBLE(json) = f;
	return json;
}


/* Allocate a json_t for a given boolean */
json_t *json_debug_boolean(const char *file, int line, int boolean)
{
	if (boolean)
		return json_debug_simple(file, line, "true", 4, JSON_BOOLEAN);
	else
		return json_debug_simple(file, line, "false", 5, JSON_BOOLEAN);
}

/* Allocate a json_t for null */
json_t *json_debug_null(const char *file, int line)
{
	return json_debug_simple(file, line, "", 0, JSON_NULL);
}

/* Allocate a json_t for a given error */
json_t *json_debug_error_null(const char *file, int line, char *fmt, ...)
{
	char	buf[200], *bigbuf;
	int	len;
	va_list	ap;
	json_t	*result;

	/* First try it in a modest buffer.  Usually works. */
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (len < 0)
		return json_debug_null(file, line);
	if (len <= sizeof buf)
		return json_debug_simple(file, line, buf, len - 1, JSON_NULL);

	/* Allocate a larger buffer to hold the string, and use it */
	bigbuf = (char *)malloc(len);
	va_start(ap, fmt);
	vsnprintf(bigbuf, len, fmt, ap);
	va_end(ap);
	result = json_debug_simple(file, line, buf, len - 1, JSON_NULL);
	free(bigbuf);
	return result;
}

/* Allocate a json_t for a key.  If value is non-NULL, then it will be used
 * as the value associated with the key.
 */
json_t *json_debug_key(const char *file, int line, const char *key, json_t *value)
{
	json_t *json = json_debug_simple(file, line, key, strlen(key), JSON_KEY);
	json->first = value;
	return json;
}

/* Allocate a json_t for an empty object */
json_t *json_debug_object(const char *file, int line)
{
	return json_debug_simple(file, line, NULL, 0, JSON_OBJECT);
}

/* Allocate a json_t for an empty array */
json_t *json_debug_array(const char *file, int line)
{
	return json_debug_simple(file, line, NULL, 0, JSON_ARRAY);
}

/* Allocate a JSON_DEFER node */
json_t *json_debug_defer(const char *file, int line, jsondeffns_t *fns)
{
	json_t *json;
	size_t	size;

	/* If no "fns" then use dummy */
	if (!fns)
		fns = &dummyfns;
		
	/* Allocate it, with extra space for an overall size of fns->size */
	size = fns->size - sizeof json->text;
	json = json_debug_simple(file, line, "", size, JSON_DEFER);

	/* Store the fns pointer, with this deferred array's implementation
	 * functions.  The rest of the jsondef_t is already initialized to 0's
	 */
	((jsondef_t *)json)->fns = fns; 

	/* Return it */
	return json;
}


/* This is called via json_walk() to tweak the source line for tracking memory leaks */
static int fixslot(json_t *json, void *data)
{
	int	slot = *(int *)data;

	/* If already fixed, leave it */
	if (json->memslot == slot)
		return 0;

	/* Change it, adjusting counts too */
	if (json->memslot != 0)
		memory_tracker[json->memslot].count--;
	if (slot != 0)
		memory_tracker[slot].count++;
	json->memslot = slot;
	return 0;
}

/* Parse a string and return it */
json_t *json_debug_parse_string(const char *file, int line, const char *str)
{
        json_t *json;
        int slot = memory_slot(file, line);
        json = json_parse_string(str);
        json_walk(json, fixslot, &slot);
        return json;
}

/* Find the first element of a (possibly deferred) array */
json_t *json_debug_first(const char *file, int line, json_t *array)
{
	int slot = memory_slot(file, line);
	json_t *json = json_first(array);
	if (json_is_deferred_element(json)) {
		fixslot(json, &slot);
		fixslot(json->next, &slot);
	}
	return json;
}

/* Do a deep copy of a json_t tree */
json_t *json_debug_copy(const char *file, int line, json_t *json)
{
        int slot = memory_slot(file, line);
        json = json_copy(json);
        json_walk(json, fixslot, &slot);
        return json;
}

/* Do a deep copy of a json_t tree, filtering items through function */
json_t *json_debug_copy_filter(const char *file, int line, json_t *json, int (*test)(json_t *elem))
{
        int slot = memory_slot(file, line);
        json = json_copy_filter(json, test);
        json_walk(json, fixslot, &slot);
        return json;
}

/* Evaluate an expression */
json_t *json_debug_calc(const char *file, int line, jsoncalc_t *calc, jsoncontext_t *context, void *agdata)
{
	int slot = memory_slot(file, line);
	json_t *json = json_calc(calc, context, agdata);
	json_walk(json, fixslot, &slot);
	return json;
}
