#ifndef SPAWN_H
#define SPAWN_H

enum {
	READ = 1,
	WRITE = 2,
};

void spawn(const char *path, char *const argv[], int mode, int fd[2]);

#endif
