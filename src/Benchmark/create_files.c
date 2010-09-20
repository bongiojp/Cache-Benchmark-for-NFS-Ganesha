#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAXPATHLEN 100

int create_dirname(char *testfile, char *path, int dir) {
  int len = strlen(path);
  strcpy(testfile, path);

  /* Add numerically named files and directories to end of export path */
  /* This is where it is assumed test files will reside. */
  len += snprintf(testfile+len, MAXPATHLEN - len, "%d", dir);
  testfile[len] = 0;
  return len;
}

int create_filename(char *testfile, char *path, int dir, int file) {
  int len = strlen(path);
  strcpy(testfile, path);

  /* Add numerically named files and directories to end of export path */
  /* This is where it is assumed test files will reside. */
  len += snprintf(testfile+len, MAXPATHLEN - len, "%d/%d", dir, file);
  testfile[len] = 0;
  return len;
}

int main() {
  int dir = 3000;
  int file = 5000;
  //  int dir = 3;
  //  int file = 5;
  int fd;
  char path[MAXPATHLEN];
  int i,j;

  for(i=0; i < dir; i++) {
    create_dirname(path, "/ibm/fs1/cache_benchmark/", i);
    printf("%s\n", path);
    mkdir(path, S_IRWXU|S_IRWXG);
    for(j=0; j < file; j++) {
      create_filename(path, "/ibm/fs1/cache_benchmark/", i, j);
      //      printf("%s\n", path);
      fd = open(path, O_CREAT, S_IRWXU|S_IRWXG);
      close(fd);
    }
  }

  return 0;
}
