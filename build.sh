set -x
gcc -Wall jsonfs.c $(pkg-config fuse json-c --cflags --libs) -o fuse_example
