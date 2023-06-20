# Simple File System using FUSE

This program implements a simple file system using FUSE (Filesystem in Userspace). It allows creating, reading, writing, and deleting files and directories. The file system is stored in a JSON file, which is loaded into memory at startup and saved back to the JSON file when the program exits.

## Requirements

- FUSE library (version 2.6 or later)
- json-c library
- pthread library

## Compilation

To compile the program, use the following command:

```
./build.sh
```

## Usage

Mount the file system by running the compiled program and specifying the mount point:

```
./fuse_example <mount_point>
```

Unmount the file system by running the following command:

```
fusermount -u <mount_point>
```

## File System Operations

The program provides the following file system operations:

- `getattr`: Retrieve file attributes.
- `open`: Open a file.
- `read`: Read file data.
- `readdir`: Read directory entries.
- `truncate`: Truncate a file.
- `write`: Write file data.
- `create`: Create a new file.
- `utimens`: Update file timestamps.
- `mkdir`: Create a new directory.
- `unlink`: Delete a file.
- `rmdir`: Delete a directory.

## JSON File Format

The file system is stored in a JSON file with the following format:

```json
[
  {
    "inode": 0,
    "type": "dir",
    "name": "/",
    "entries": [
      {
        "name": "file1.txt",
        "inode": 1
      },
      {
        "name": "dir1",
        "inode": 2
      }
    ]
  },
  {
    "inode": 1,
    "type": "reg",
    "name": "file1.txt",
    "data": "File 1 content"
  },
  {
    "inode": 2,
    "type": "dir",
    "name": "dir1",
    "entries": [
      {
        "name": "file2.txt",
        "inode": 3
      }
    ]
  },
  {
    "inode": 3,
    "type": "reg",
    "name": "file2.txt",
    "data": "File 2 content"
  }
]
```

- Each object represents a file or directory in the file system.
- The `inode` field is a unique identifier for each file or directory.
- The `type` field can be either `"reg"` for regular files or `"dir"` for directories.
- The `name` field specifies the name of the file or directory.
- For regular files, the `data` field stores the file content.
- For directories, the `entries` field is an array of objects representing the directory contents.

## Synchronization

The program uses a mutex (`fs_mutex`) to synchronize access to the file system data structures. The mutex is locked before accessing or modifying the file system data and unlocked afterward, ensuring exclusive access to the data and preventing conflicts.

