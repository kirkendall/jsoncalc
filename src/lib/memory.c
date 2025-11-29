/* Memory.c
 *
 * This file contains a variety of low-level jx_t allocation functions,
 * and the jx_free() function for deallocating them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <jx.h>

/* Here we need to access the "real" allocation/free functions */
#ifdef JX_DEBUG_MEMORY
# undef jx_free
# undef jx_simple
# undef jx_string
# undef jx_number
# undef jx_from_int
# undef jx_from_double
# undef jx_boolean
# undef jx_null
# undef jx_error_null
# undef jx_array
# undef jx_key
# undef jx_object
# undef jx_defer
# undef jx_first
# undef jx_parse_string
# undef jx_copy
# undef jx_copy_filter
# undef jx_calc
#endif

/* For debugging, this is used to store places that allocate memory, and
 * how many nodes were allocated there without freeing.  If a program links
 * with this library without defining JX_DEBUG_MEMORY then this will be
 * unused, but it's fairly small.
 */
typedef struct
{
        const char *file;
        int  line;
        int  count;
} memory_tracker_t;
static memory_tracker_t *memory_tracker;

/* This counts the number of jx_t's currently allocated.  Not threadsafe! */
int jx_debug_count = 0;

/* Return an estimated byte count for a given jx_t tree */
size_t jx_sizeof(jx_t *json)
{
        size_t size = 0;
        size_t len;

        while (json) {
                size += sizeof(jx_t);

                switch (json->type) {
                  case JX_STRING:
                  case JX_NUMBER:
                  case JX_BOOLEAN:
                  case JX_NULL:
                  case JX_KEY:
                        /* Add the text length including the terminating \0,
                         * tweaked for alignment
                         */
                        len = ((strlen(json->text) - sizeof(json->text)) | 0x1f) + 1;
                        size += len;
                        break;

                  case JX_DEFER:
			/* Rough guess: twice the size of a jx_t.  We count
			 * one jx_t after this switch, so we just add 1 here.
			 */
			size += sizeof(jx_t);
			break;

                  case JX_ARRAY:
                  case JX_OBJECT:
                  case JX_BADTOKEN:
                  case JX_NEWLINE:
                  case JX_ENDARRAY:
                  case JX_ENDOBJECT:
                        ; /* Listed just to keep the compiler happy */
                }

                /* Recursively add the "first" data */
                size += jx_sizeof(json->first);

                /* Iteratively add the "next" data */
                json = json->next; /* undeferred */
        }

        return size;
}

/* Free a JSON data tree */
void jx_free(jx_t *json)
{
        jx_t *next;

	/* Defend against NULL */
	if (!json)
		return;

        /* If this was allocated with memory debugging turned on, but freed
         * where it isn't turned on, then complain.
         */
	assert(json->memslot == 0 || memory_tracker[json->memslot].line != 0);

	/* If ->next points to a JX_DEFER, that means this jx_t is an
	 * element of a deferred array, currently being scanned.  Free the
	 * resources used for this scan session.
	 */
	if (json->next && json->next->type == JX_DEFER) {
		jxdeffns_t *fns = (jxdeffns_t *)json->next;
		if (fns->free)
			(*fns->free)(json);
	}

	/* Iteratively free this node and its siblings */
	while (json) {

		/* If this is a JX_DEFER array, then free its resources
		 * specially
		 */
		if (json->first && json->first->type == JX_DEFER) {
			jxdef_t *def = (jxdef_t *)json->first;
			if (def->fns->free)
				(*def->fns->free)(json);
		}

		/* Recursively free contained data.  Note that JX_NULL nodes
		 * abuse the ->first field to store a pointer into source text
		 * instead of a jx_t.
		 */
		if (json->type != JX_NULL)
			jx_free(json->first);

		/* Free this jx_t struct */
		next = json->next; /* undeferred */
		free(json);
		jx_debug_count--;
		json = next;
	}
}

/* Allocate a jx_t node and initialize some fields */
jx_t *jx_simple(const char *str, size_t len, jxtype_t type)
{
	jx_t *json;
	size_t  size;

	/* String is optional.  If omitted then use "" as a placeholder */
	if (!str)
		len = 0;
	else if (len == (size_t)-1)
		len = strlen(str);

        /* Compute the size, rounding up to a multiple of 32 */
        size = sizeof(jx_t) - sizeof json->text + len + 1;
        size = (size | 0x1f) + 1;

	/* Allocate it.  Trust malloc() to be efficient */
        json = (jx_t *)malloc(size);

	/* Initialize the fields */
	memset(json, 0, size);
	json->type = type;
	if (str)
		strncpy(json->text, str, len);

	/* return it */
	jx_debug_count++;
	return json;
}

/* Allocate a jx_t for a given string.  Note that any escape sequences
 * such as \n or \u22c8 are handled by the parser via jx_mbs_unescape()
 * so we just get the actual data here.  Passing -1 for len causes it to
 * compute the length via strlen().  "str" is not required to have a
 * terminating '\0' but the returned json->text field will.
 */
jx_t *jx_string(const char *str, size_t len)
{
	return jx_simple(str, len, JX_STRING);
}

/* Allocate a jx_t for a given number, expressed as a string.  If you pass
 * -1 for len, it'll compute the length via strlen().
 */
jx_t *jx_number(const char *str, size_t len)
{
	return jx_simple(str, len, JX_NUMBER);
}

/* Allocate a jx_t for a given integer */
jx_t *jx_from_int(int i)
{
	jx_t *json = jx_number("", 0);
	json->text[1] = 'i';
	JX_INT(json) = i;
	return json;
}

/* Allocate a jx_t for a given floating-point number */
jx_t *jx_from_double(double f)
{
	jx_t *json = jx_number("", 0);
	json->text[1] = 'd';
	JX_DOUBLE(json) = f;
	return json;
}

/* Allocate a jx_t for a boolean value. */
jx_t *jx_boolean(int boolean)
{
	if (boolean)
		return jx_simple("true", 4, JX_BOOLEAN);
	else
		return jx_simple("false", 5, JX_BOOLEAN);
}

/* Allocate a jx_t for a null value */
jx_t *jx_null(void)
{
	return jx_simple("", 0, JX_NULL);
}

/* Allocate a jx_t for a null value, encoding an error message */
jx_t *jx_error_null(const char *where, const char *fmt, ...)
{
	char	buf[200], *bigbuf;
	int	len;
	va_list	ap;
	jx_t	*result;

	/* First try it in a modest buffer.  Usually works. */
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (len < 0)
		return jx_null();
	if (len <= sizeof buf - 1) {
		result = jx_simple(buf, len, JX_NULL);
		result->first = (jx_t *)where;
	}

	/* Allocate a larger buffer to hold the string, and use it */
	bigbuf = (char *)malloc(len);
	va_start(ap, fmt);
	vsnprintf(bigbuf, len, fmt, ap);
	va_end(ap);
	result = jx_simple(buf, len, JX_NULL);
	free(bigbuf);
	result->first = (jx_t *)where;
	return result;
}

/* Allocate a jx_t for a key.  The value must be non-NULL (though it can be
 * jx_null() ).  Later, you can use jx_append() to change the value.
 */
jx_t *jx_key(const char *key, jx_t *value)
{
	assert(value != NULL);

	/* Allocate it with twice as much space for storing the key's name.
	 * This is so we can also store the simplified version later, if
	 * necessary.
	 */
	jx_t *json = jx_simple(key, strlen(key) * 2 + 1, JX_KEY);
	json->first = value;
	return json;
}

/* Allocate a jx_t for an empty object */
jx_t *jx_object()
{
	return jx_simple(NULL, 0, JX_OBJECT);
}

/* Allocate a jx_t for an empty array */
jx_t *jx_array()
{
	return jx_simple(NULL, 0, JX_ARRAY);
}

/* This is a dummy type of deferred array which is always empty.  It is used
 * when jx_defer() is called with NULL instead of a real list of deferred
 * functions.
 */
static jx_t *dummy(jx_t *node)
{
	return NULL;
};
static jxdeffns_t dummyfns = {
	sizeof(jx_t) + sizeof(jxdeffns_t),	/* size (of the whole more-than-jx_t */
	"Dummy",	/* desc */
	dummy,		/* first */
	dummy,		/* next */
	NULL,		/* islast */
	NULL,		/* free */
	NULL,		/* byindex */
	NULL		/* bykey */
};

/* Allocate a JX_DEFER node.  "fns" is a collection of function pointers
 * that implement the desired type of deferred array, or NULL to use a
 * dummy set of functions.
 */
jx_t *jx_defer(jxdeffns_t *fns)
{
	jx_t *json;
	size_t	size;

	/* If no "fns" then use dummy */
	if (!fns)
		fns = &dummyfns;

	/* Allocate it, with extra space.  Note that we must tweak the size
	 * because jx_simple wants to be passed the size of the "text" field,
	 * but fns->size is the size of the whole thing.
	 */
	size = fns->size - sizeof(jx_t) + sizeof json->text;
	json = jx_simple(NULL, size, JX_DEFER);

	/* Store the fns pointer, with this deferred array's implementation
	 * functions.  The rest of the jxdef_t is already initialized to 0's
	 */
	((jxdef_t *)json)->fns = fns; 

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
#ifdef JX_DEBUG_MEMORY
	jx_debug_free(__FILE__, __LINE__, jx_system);
#endif
        for (i = 0; i < 4096; i++)
                if (memory_tracker[i].count > 0)
                        fprintf(stderr, "%s:%d: Leaked %d jx_t's\n", memory_tracker[i].file, memory_tracker[i].line, memory_tracker[i].count);
        if (memory_tracker[4096].count > 0)
                fprintf(stderr, "Leaked %d jx_t's from an untracked source\n", memory_tracker[4096].count);
}

/* For debugging, this looks for a slot for counting allocations from a given
 * source line.
 */
static int memory_slot(const char *file, int line)
{
        int     slot, start;

        /* If memory tracking hasn't been initialized, then do so now */
        if (!memory_tracker) {
                /* Allocate memory for the tracker.  Each jx_t has a 12-bit
                 * field for tracking its allocation source, so we want 4096
                 * tracker slots.  We also want one more slot in case there
                 * are more than 4096 source lines that allocate jx_t's.
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
                        return slot;
                }

                /* Found an existing slot for this file/line */
                if (memory_tracker[slot].file == file && memory_tracker[slot].line == line) {
                        return slot;
		}

                /* Bumped to next slot */
                slot = (slot & 0xfff) + 1;
        } while (slot != start);

        /* If we get here then we looped without ever finding the slot or an
         * empty slot.  All we can do is count it in the overflow area.
         */
        return 0;
}


/* The following are debugging wrappers around the above functions.  They are
 * normally only called if the source program defines JX_DEBUG_MEMORY but
 * we want to use the same library whether debugging is being used or not,
 * so these are defined unconditionally.
 */
void jx_debug_free(const char *file, int line, jx_t *json)
{
	jx_t *next;

        /* Iterate over the ->next links */
        for (; json; json = next)
        {
		next = json->next; /* undeferred */

		/* If it looks like it was already freed, then complain.
		 * This isn't reliable!  It won't reject valid free's but it
		 * might miss some invalid ones, if the memory was recycled.
		 */
		if (json->type == JX_BADTOKEN) {
			fprintf(stderr, "%s:%d: Attempt to free memory twice\n", file, line);
			if (json->memslot)
				fprintf(stderr, "%s:%d: This is where it was first freed\n", memory_tracker[json->memslot].file, -memory_tracker[json->memslot].line);
		}

		/* We track by where the jx_t is allocated not by where it
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
		if (!jx_is_error(json))
			jx_debug_free(file, line, json->first);

		/* Free this node */
		json->first = json->next = NULL; /* undeferred */
		json->memslot = memory_slot(file, -line);
		jx_free(json);
	}
}

jx_t *jx_debug_simple(const char *file, int line, const char *str, size_t len, jxtype_t type)
{
        jx_t  *json;
        int slot = memory_slot(file, line);
        memory_tracker[slot].count++;
        json = jx_simple(str, len, type);
        json->memslot = slot;
        return json;
}

/* Allocate a jx_t for a given string. */
jx_t *jx_debug_string(const char *file, int line, const char *str, size_t len)
{
        return jx_debug_simple(file, line, str, len, JX_STRING);
}

/* Allocate a jx_t for a given number, expressed as a string */
jx_t *jx_debug_number(const char *file, int line, const char *str, size_t len)
{
	return jx_debug_simple(file, line, str, len, JX_NUMBER);
}

/* Allocate a jx_t for a given integer */
jx_t *jx_debug_from_int(const char *file, int line, int i)
{
	jx_t	*json = jx_debug_number(file, line, "", 0);
	json->text[1] = 'i';
	JX_INT(json) = i;
	return json;
}

/* Allocate a jx_t for a given floating-point number */
jx_t *jx_debug_from_double(const char *file, int line, double f)
{
	jx_t	*json = jx_debug_number(file, line, "", 0);
	json->text[1] = 'd';
	JX_DOUBLE(json) = f;
	return json;
}


/* Allocate a jx_t for a given boolean */
jx_t *jx_debug_boolean(const char *file, int line, int boolean)
{
	if (boolean)
		return jx_debug_simple(file, line, "true", 4, JX_BOOLEAN);
	else
		return jx_debug_simple(file, line, "false", 5, JX_BOOLEAN);
}

/* Allocate a jx_t for null */
jx_t *jx_debug_null(const char *file, int line)
{
	return jx_debug_simple(file, line, "", 0, JX_NULL);
}

/* Allocate a jx_t for a given error */
jx_t *jx_debug_error_null(const char *file, int line, char *fmt, ...)
{
	char	buf[200], *bigbuf;
	int	len;
	va_list	ap;
	jx_t	*result;

	/* First try it in a modest buffer.  Usually works. */
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (len < 0)
		return jx_debug_null(file, line);
	if (len <= sizeof buf)
		return jx_debug_simple(file, line, buf, len - 1, JX_NULL);

	/* Allocate a larger buffer to hold the string, and use it */
	bigbuf = (char *)malloc(len);
	va_start(ap, fmt);
	vsnprintf(bigbuf, len, fmt, ap);
	va_end(ap);
	result = jx_debug_simple(file, line, buf, len - 1, JX_NULL);
	free(bigbuf);
	return result;
}

/* Allocate a jx_t for a key.  If value is non-NULL, then it will be used
 * as the value associated with the key.
 */
jx_t *jx_debug_key(const char *file, int line, const char *key, jx_t *value)
{
	/* Allocate double the space for the key name, so we have a place to
	 * put the "loose" version from jx_mbs_simple_key().
	 */
	jx_t *json = jx_debug_simple(file, line, key, strlen(key) * 2 + 1, JX_KEY);
	json->first = value;
	return json;
}

/* Allocate a jx_t for an empty object */
jx_t *jx_debug_object(const char *file, int line)
{
	return jx_debug_simple(file, line, NULL, 0, JX_OBJECT);
}

/* Allocate a jx_t for an empty array */
jx_t *jx_debug_array(const char *file, int line)
{
	return jx_debug_simple(file, line, NULL, 0, JX_ARRAY);
}

/* Allocate a JX_DEFER node */
jx_t *jx_debug_defer(const char *file, int line, jxdeffns_t *fns)
{
	jx_t *json;
	size_t	size;

	/* If no "fns" then use dummy */
	if (!fns)
		fns = &dummyfns;
		
	/* Allocate it, with extra space for an overall size of fns->size */
	size = fns->size - sizeof json->text;
	json = jx_debug_simple(file, line, "", size, JX_DEFER);

	/* Store the fns pointer, with this deferred array's implementation
	 * functions.  The rest of the jxdef_t is already initialized to 0's
	 */
	((jxdef_t *)json)->fns = fns; 

	/* Return it */
	return json;
}


/* This is called via jx_walk() to tweak the source line for tracking memory leaks */
static int fixslot(jx_t *json, void *data)
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
jx_t *jx_debug_parse_string(const char *file, int line, const char *str)
{
        jx_t *json;
        int slot = memory_slot(file, line);
        json = jx_parse_string(str);
        jx_walk(json, fixslot, &slot);
        return json;
}

/* Find the first element of a (possibly deferred) array */
jx_t *jx_debug_first(const char *file, int line, jx_t *array)
{
	int slot = memory_slot(file, line);
	jx_t *json = jx_first(array);
	if (jx_is_deferred_element(json)) {
		fixslot(json, &slot);
		fixslot(json->next, &slot);
	}
	return json;
}

/* Do a deep copy of a jx_t tree */
jx_t *jx_debug_copy(const char *file, int line, jx_t *json)
{
        int slot = memory_slot(file, line);
        json = jx_copy(json);
        jx_walk(json, fixslot, &slot);
        return json;
}

/* Do a deep copy of a jx_t tree, filtering items through function */
jx_t *jx_debug_copy_filter(const char *file, int line, jx_t *json, int (*test)(jx_t *elem))
{
        int slot = memory_slot(file, line);
        json = jx_copy_filter(json, test);
        jx_walk(json, fixslot, &slot);
        return json;
}

/* Evaluate an expression */
jx_t *jx_debug_calc(const char *file, int line, jxcalc_t *calc, jxcontext_t *context, void *agdata)
{
	int slot = memory_slot(file, line);
	jx_t *json = jx_calc(calc, context, agdata);
	jx_walk(json, fixslot, &slot);
	return json;
}
