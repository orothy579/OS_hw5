set -x
gcc -Wall fuse.c $(pkg-config fuse json-c --cflags --libs) -o fuse_example
