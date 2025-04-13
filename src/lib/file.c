#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

/* This can be set to 'o' to make new/unreadable files contain an empty object,
 * or 'a' to make new/unreadable files contain an empty array.  If left to '\0'
 * then new/unreadable files will just fail to load.
 */
char json_file_new_type = '\0';

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
	jf->isfile = (st.st_mode & S_IFMT) == S_IFREG;
	jf->size = st.st_size;
	jf->base = base;
	return jf;
}

/* Close a file that was opened via json_file_open_to_read() */
void json_file_unload(jsonfile_t *jf)
{
	if (jf->isfile) {
		/* Unmap the file from memory */
		munmap((void *)jf->base, jf->size);
	} else {
		/* Free the buffer */
		free((void *)jf->base);
	}

	/* Close the file.  This will also release the file lock */
	if (jf->fd != 0) /* never close stdin */
		close(jf->fd);

	/* Free the jf structure */
	free(jf);
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

/* Scan $JSONCALCPATH for a given file.  If found, return its full pathname
 * as a dynamically-allocated string (which the calling function must free).
 * If not found, return NULL.  If "filename" is NULL then just look for a
 * writable directory in the path.  If "ext" is non-NULL then append it to
 * the filename.
 */
char *json_file_path(const char *filename, const char *ext)
{
	char	*pathname;	/* dynamically-allocated pathname */
	size_t	pathsize;	/* allocated size of pathname */
	char	*home;		/* User's home directory */
	char	*jsoncalcpath;	/* Value of the $JSONCALCPATH or a default */
	char	*strtok_context;/* used internally by strtok_r() */
	char	*dir;		/* A directory from the path */
	size_t	needsize;	/* Size of the pathname we're considering */
	int	first;

	/* If no ext then use "" */
	if (!filename)
		filename = "";
	if (!ext)
		ext = "";

	/* Start with a modest pathname buffer */
	pathsize = 64;
	pathname = (char *)malloc(pathsize);

	/* Get the home directory.  If not set, then assume "." */
	home = getenv("HOME");
	if (!home)
		home = ".";

	/* Get the path from $JSONCALCPATH, or use a default */
	jsoncalcpath = getenv("JSONCALCPATH");
	if (!jsoncalcpath)
		jsoncalcpath = JSON_PATH_DEFAULT;

	/* Make a copy of the path -- strtok_r() mangles it */
	jsoncalcpath = strdup(jsoncalcpath);

	/* For each entry in the path... */
	first = 1;
	for (dir = strtok_r(jsoncalcpath, JSON_PATH_DELIM, &strtok_context);
	     dir;
	     dir = strtok_r(NULL, JSON_PATH_DELIM, &strtok_context)) {
		/* Generate the filename in this directory.  If the directory
		 * starts with "~" then use $HOME instead.
		 */
		if (*dir == '~')
			needsize = snprintf(pathname, pathsize, "%s%s/%s%s", home, dir + 1, filename, ext);
		else if (*dir == '.' && !dir[1])
			needsize = snprintf(pathname, pathsize, "%s%s", filename, ext);
		else
			needsize = snprintf(pathname, pathsize, "%s/%s%s", dir, filename, ext);

		/* If necessary, expand the buffer and try again */
		if (needsize > pathsize) {
			pathname = (char *)realloc(pathname, needsize);
			pathsize = needsize;
			if (*dir == '~')
				snprintf(pathname, pathsize, "%s%s/%s%s", home, dir + 1, filename, ext);
			else if (*dir == '.' && !dir[1])
				snprintf(pathname, pathsize, "%s%s", filename, ext);
			else
				snprintf(pathname, pathsize, "%s/%s%s", dir, filename, ext);
		}

		/* If this is the first entry in the path list, and we're
		 * looking for a writable directory, and this directory
		 * doesn't exist, then try to create it.
		 */
		if (first && !*filename && access(pathname, W_OK)) {
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
		if (*filename) {
			/* File -- if this pathname is readable, return it. */
			if (access(pathname, R_OK) == 0) {
				free(jsoncalcpath);
				return pathname;
			}
		} else {
			/* Directory -- if pathname is writable, return it */
			if (access(pathname, W_OK) == 0) {
				free(jsoncalcpath);
				return pathname;
			}
		}
	}

	/* Not found -- clean up and return NULL */
	free(jsoncalcpath);
	free(pathname);
	return NULL;
}
