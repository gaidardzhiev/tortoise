#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static char buf[4096];

int main(void) {
	int fd = open("/big.bin", O_RDWR | O_CREAT | O_TRUNC), i, bad = 0;
	unsigned total = 8700 * 1024, off;
	struct stat st;
	for (off = 0; off < total; off += sizeof(buf)) {
		for (i = 0; i < (int)sizeof(buf); i += 4) *(unsigned *)(buf + i) = off + i;
		if (write(fd, buf, sizeof(buf)) != (int)sizeof(buf)) {
			printf("write failed at %u\n", off);
			return 1;
		}
	}
	fstat(fd, &st);
	printf("wrote %u bytes, %u blocks\n", st.st_size, st.st_blocks);
	for (off = 0; off < total; off += 1031 * 512) {
		unsigned v;
		lseek(fd, (int)(off & ~3u), SEEK_SET);
		read(fd, &v, 4);
		if (v != (off & ~3u)) bad++;
	}
	lseek(fd, (int)total - 4, SEEK_SET);
	{
		unsigned v;
		read(fd, &v, 4);
		if (v != total - 4) bad++;
	}
	printf("read back %s\n", bad ? "WRONG" : "correctly, including the triple indirect part");
	ftruncate(fd, 100000);
	fstat(fd, &st);
	printf("truncated to %u\n", st.st_size);
	close(fd);
	unlink("/big.bin");
	fd = open("/big2.bin", O_RDWR | O_CREAT);
	for (off = 0; off < total; off += sizeof(buf))
		if (write(fd, buf, sizeof(buf)) != (int)sizeof(buf)) {
			printf("space was not freed\n");
			return 1;
		}
	close(fd);
	unlink("/big2.bin");
	printf("space reused after unlink\n");
	return 0;
}
