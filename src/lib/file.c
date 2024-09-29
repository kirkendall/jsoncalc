#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

/* Open a file for reading.  This also locks one byte, and maps it into memory*/
jsonfile_t *json_file_load(const char *filename)
{
	int	fd;
	char	*base;
	jsonfile_t *jf;
	struct stat st;

	/* Open the file */
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	/* Get its size */
	fstat(fd, &st);

	/* Lock one byte of the file. */
	lseek(fd, (off_t)getpid(), SEEK_SET);
	lockf(fd, F_LOCK, (off_t)1);

	/* Map the file into memory */
	base = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, (off_t)0);

	/* Return the info */
	jf = (jsonfile_t *)malloc(sizeof *jf);
	jf->fd = fd;
	jf->size = st.st_size;
	jf->base = base;
	return jf;
}

/* Close a file that was opened via json_file_open_to_read() */
void json_file_unload(jsonfile_t *jf)
{
	/* Unmap the file from memory */
	munmap((void *)jf->base, jf->size);

	/* Close the file.  This will also release the file lock */
	close(jf->fd);
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
	fd = open(filename, O_WRONLY|O_CREAT, 0666);
	if (fd < 0)
		return NULL;

	/* Lock the entire file.  If any other writers or readers have
	 * locked even a single byte, this will wait until they're done.
	 */
	lockf(fd, F_LOCK, (off_t)0);

	/* Okay now we can truncate the file */
	ftruncate(fd, (off_t)0);

	/* Add stdio buffering, and return it */
	return fdopen(fd, "w");
}
