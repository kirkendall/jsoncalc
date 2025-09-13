#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <jsoncalc.h>

/* This can be set to 'o' to make new/unreadable files contain an empty object,
 * or 'a' to make new/unreadable files contain an empty array.  If left to '\0'
 * then new/unreadable files will just fail to load.
 */
char json_file_new_type = '\0';

/* This stores a linked list of loaded files */
static jsonfile_t *loaded;

/* Open a file for reading.  This also locks one byte and maps it into memory */
jsonfile_t *json_file_load(const char *filename)
{
	int	fd;
	char	*base;
	jsonfile_t *jf;
	struct stat st;
	size_t	size, used, nread;

	/* Open the file */
	if (!strcmp(filename, "-"))
		fd = 0; /* stdin */
	else
		fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		if (json_file_new_type == 'o' || json_file_new_type == 'a') {
			jf = (jsonfile_t *)malloc(sizeof(jsonfile_t));
			jf->fd = -1;
			jf->isfile = 0;
			jf->size = 3;
			if (json_file_new_type == 'o')
				jf->base = strdup("{}\n");
			else
				jf->base = strdup("[]\n");
			return jf;
		}
		return NULL;
	}

	/* Get its type and size */
	fstat(fd, &st);

	/* Is it a regular file? */
	if ((st.st_mode & S_IFMT) == S_IFREG) {
		/* Lock one byte of the file. */
		lseek(fd, (off_t)getpid(), SEEK_SET);
		lockf(fd, F_LOCK, (off_t)1);

		/* Map the file into memory */
		base = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, (off_t)0);
	} else {
		/* Read into a dynamically-allocated buffer */
		size = 4096;
		base = (char *)malloc(size);
		for (used = 0; (nread = read(fd, base + used, size - used)) > 0; used += nread) {
			if (size - (used + nread) < 1024) {
				size *= 2;
				base = (char *)realloc(base, size);
			}
		}

		/* Got it all.  Trim the excess, rounding up to a multiple of
		 * 4096 and keeping one extra byte as a '\0'.
		 */
		size = ((used + 1) | 0x3ff) + 1;
		base = (char *)realloc(base, size);
		st.st_size = size;
	}

	/* Return the info */
	jf = (jsonfile_t *)malloc(sizeof *jf);
	jf->fd = fd;
	jf->filename = strdup(filename);
	jf->isfile = (st.st_mode & S_IFMT) == S_IFREG;
	jf->size = st.st_size;
	jf->base = base;
	jf->other = loaded;
	loaded = jf;
	return jf;
}

/* Close a file that was opened via json_file_load() */
void json_file_unload(jsonfile_t *jf)
{
	/* Remove the jf structure from the loaded list */
	jsonfile_t *scan, *lag;
	for (lag = NULL, scan = loaded;
	     scan && scan != jf;
	     lag = scan, scan = scan->other) {
	}
	if (!scan) {
		/* Should never happen */
		abort();
	} else if (lag) {
		lag->other = scan->other;
	} else
		loaded = scan->other;

	/* Unmap or free the memory */
	if (jf->isfile) {
		/* Unmap the file from memory */
		munmap((void *)jf->base, jf->size);
	} else {
		/* Free the buffer */
		free((void *)jf->base);
	}

	/* Free the filename */
	free(jf->filename);

	/* Close the file.  This will also release the file lock */
	if (jf->fd != 0) /* never close stdin */
		close(jf->fd);

	/* Free the jf structure */
	free(jf);
}

/* Figure out which file contains a given pointer, and return it.  If refline
 * isn't NULL, then store the line number there.  If no loaded file contains
 * the given "where" pointer, return NULL. The "where" pointer is never
 * dereferenced, so you can be a bit sloppy about it.
 */
jsonfile_t *json_file_containing(const char *where, int *refline)
{
	jsonfile_t *jf;
	const char *scan;
	int	line;

	/* Scan for it */
	for (jf = loaded; jf; jf = jf->other) {
		if (jf->base <= where && where < jf->base + jf->size) {
			/* Found it! */

			/* Maybe figure out the line number */
			if (refline) {
				line = 1;
				for (scan = jf->base; scan < where; scan++)
					if (*scan == '\n')
						line++;
				*refline = line;
			}

			/* Return it */
			return jf;
		}
	}

	/* Not found */
	return NULL;
}

/* Open a file for writing.  This locks the whole file -- that's the only
 * difference between this and fopen(filename,"w").  When done, the FILE*
 * should be closed via the conventional fclose() function.
 */
FILE *json_file_update(const char *filename)
{
	int	fd;

	/* Open the file.  Note that we do *NOT* truncate it right away,
	 * because some other process might still be reading it.
	 */
	if (!strcmp(filename, "-"))
		fd = dup(1); /* stdout */
	else {
		fd = open(filename, O_WRONLY|O_CREAT, 0666);
		if (fd < 0)
			return NULL;

		/* Lock the entire file.  If any other writers or readers have
		 * locked even a single byte, this will wait until they're done.
		 */
		lockf(fd, F_LOCK, (off_t)0);

		/* Okay now we can truncate the file */
		ftruncate(fd, (off_t)0);
	}

	/* Add stdio buffering, and return it */
	return fdopen(fd, "w");
}

/* Scan JsonCalc's path for a given file.  If found, return its full pathname
 * as a dynamically-allocated string (which the calling function must free).
 * If not found, return NULL.  If "filename" is NULL then just look for a
 * writable directory in the path.  If "ext" is non-NULL then append it to
 * the filename.
 */
char *json_file_path(const char *prefix, const char *name, const char *suffix)
{
	char	*pathname;	/* dynamically-allocated pathname */
	size_t	pathsize;	/* allocated size of pathname */
	char	*home;		/* User's home directory */
	json_t	*path;		/* Array of directories to check */
	size_t	needsize;	/* Size of the pathname we're considering */
	int	first;

	/* If no ext then use "" */
	if (!prefix)
		prefix = "";
	if (!name)
		name = "";
	if (!suffix)
		suffix = "";

	/* Start with a modest pathname buffer */
	pathsize = 64;
	pathname = (char *)malloc(pathsize);

	/* Get the home directory.  If not set, then assume ".".  We use this
	 * when a path entry starts with "~/".
	 */
	home = getenv("HOME");
	if (!home)
		home = ".";

	/* Get the path from json_config */
	path = json_by_key(json_system, "path");
	if (!path || path->type != JSON_ARRAY)
		return NULL;

	/* For each entry in the path... */
	first = 1;
	for (path = path->first; path; path = path->next) { /* undeferred */
		/* Generate the filename in this directory.  If the directory
		 * starts with "~" then use $HOME instead.
		 */
		if (*path->text == '~')
			needsize = snprintf(pathname, pathsize, "%s%s/%s%s%s", home, path->text + 1, prefix, name, suffix);
		else if (*path->text == '.' && !path->text[1])
			needsize = snprintf(pathname, pathsize, "%s%s%s", prefix, name, suffix);
		else
			needsize = snprintf(pathname, pathsize, "%s/%s%s%s", path->text, prefix, name, suffix);

		/* If necessary, expand the buffer and try again */
		if (needsize > pathsize) {
			pathname = (char *)realloc(pathname, needsize);
			pathsize = needsize;
			if (*path->text == '~')
				snprintf(pathname, pathsize, "%s%s/%s%s%s", home, path->text + 1, prefix, name, suffix);
			else if (*path->text == '.' && !path->text[1])
				snprintf(pathname, pathsize, "%s%s%s", prefix, name, suffix);
			else
				snprintf(pathname, pathsize, "%s/%s%s%s", path->text, prefix, name, suffix);
		}

		/* If this is the first entry in the path list, and we're
		 * looking for a writable directory, and this directory
		 * doesn't exist, then try to create it.
		 */
		if (first && !*name && access(pathname, W_OK)) {
			char *slash;
			for (slash = pathname + 1; *slash; slash++) {
				if (*slash == '/') {
					*slash = '\0';
					mkdir(pathname, 0700);
					*slash = '/';
				}
			}
		}

		/* Are we looking for a directory or a file? */
		if (*name) {
			/* File -- if this pathname is readable, return it. */
			if (access(pathname, R_OK) == 0) {
				return pathname;
			}
		} else {
			/* Directory -- if pathname is writable, return it */
			if (access(pathname, W_OK) == 0) {
				return pathname;
			}
		}
	}

	/* Not found -- clean up and return NULL */
	free(pathname);
	return NULL;
}
