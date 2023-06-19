#define FUSE_USE_VERSION 26

#include <stdbool.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <libgen.h>

typedef struct {
    int inode;
    const char *type;
    char *name;
    char *data;
    struct json_object *entries;
} fs_object;

static fs_object *fs_objects;
static int num_fs_objects;

void print_fs_object(const fs_object *obj) {
    printf("fs_object: inode=%d, type=%s, name=%s, data=%s\n",
           obj->inode, obj->type ? obj->type : "Unknown", obj->name ? obj->name : "Unknown",
           obj->data ? obj->data : "Unknown");
}

static void load_json_fs(const char *filename) {
    struct json_object *fs_json = json_object_from_file(filename);
    if (!fs_json) {
        fprintf(stderr, "Failed to load JSON filesystem from %s\n", filename);
        exit(1);
    }

    num_fs_objects = json_object_array_length(fs_json);
    fs_objects = calloc(num_fs_objects, sizeof(fs_object));
    for (int i = 0; i < num_fs_objects; i++) {
        struct json_object *obj = json_object_array_get_idx(fs_json, i);
        struct json_object *tmp;

        if (json_object_object_get_ex(obj, "inode", &tmp))
            fs_objects[i].inode = json_object_get_int(tmp);
        if (json_object_object_get_ex(obj, "type", &tmp))
            fs_objects[i].type = json_object_get_string(tmp);
        if (json_object_object_get_ex(obj, "name", &tmp)) {
            const char *name = json_object_get_string(tmp);
            fs_objects[i].name = strdup(name);
        }
        if (json_object_object_get_ex(obj, "data", &tmp)) {
            const char *data = json_object_get_string(tmp);
            fs_objects[i].data = strdup(data);
        } else {
            fs_objects[i].data = NULL;
        }
        if (json_object_object_get_ex(obj, "entries", &tmp))
            fs_objects[i].entries = tmp;

        print_fs_object(&fs_objects[i]);
    }
}

static int lookup_inode(const char *path) {
    char *path_copy = strdup(path);
    char *seg = strtok(path_copy, "/");
    int inode = 0;  // root directory

    while (seg != NULL) {
        bool found = false;
        const fs_object *dir_obj = &fs_objects[inode];
        int entries_length = json_object_array_length(dir_obj->entries);
        for (int i = 0; i < entries_length; i++) {
            struct json_object *entry_obj = json_object_array_get_idx(dir_obj->entries, i);
            struct json_object *name_obj, *inode_obj;

            if (json_object_object_get_ex(entry_obj, "name", &name_obj) && json_object_object_get_ex(entry_obj, "inode", &inode_obj)) {
                const char *name = json_object_get_string(name_obj);
                if (strcmp(name, seg) == 0) {
                    inode = json_object_get_int(inode_obj);
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            free(path_copy);
            return -1;  // inode not found
        }

        seg = strtok(NULL, "/");
    }

    free(path_copy);
    return inode;
}


static int fuse_example_open(const char *path, struct fuse_file_info *fi) {
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

//    if ((fi->flags & 3) != O_RDONLY) {
//        return -EACCES;  // Access denied
//    }

    return 0;
}

static int fuse_example_read(const char *path, char *buf, size_t size, off_t offset,
                             struct fuse_file_info *fi) {
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

    const fs_object *obj = &fs_objects[inode];
    if (obj->data) {
        size_t len = strlen(obj->data);
        if (offset >= len) {
            return 0;
        }
        if (offset + size > len) {
            size = len - offset;
        }
        memcpy(buf, obj->data + offset, size);
    } else {
        size = 0;
    }

    return size;
}

static int fuse_example_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                                off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

    const fs_object *obj = &fs_objects[inode];

    if(strcmp(obj->type, "dir") != 0) {
        return -ENOTDIR; // Not a directory
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    int entries_length = json_object_array_length(obj->entries);
    for(int i = 0; i < entries_length; i++) {
        struct json_object *entry_obj = json_object_array_get_idx(obj->entries, i);
        struct json_object *name_obj;

        if (json_object_object_get_ex(entry_obj, "name", &name_obj)) {
            const char *name = json_object_get_string(name_obj);
            filler(buf, name, NULL, 0);
        }
    }

    return 0;
}

static int fuse_example_write(const char *path, const char *buf, size_t size, off_t offset,
                              struct fuse_file_info *fi) {
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;

    fs_object *obj = &fs_objects[inode];
    if (obj->data) {
        // Make sure the file is large enough to write the data.
        size_t new_size = offset + size;
        if (new_size > strlen(obj->data)) {
            obj->data = realloc(obj->data, new_size + 1);  // +1 for the null terminator
            if (!obj->data) return -ENOMEM;
            memset(obj->data + strlen(obj->data), 0, new_size - strlen(obj->data));
        }

        // Write the data.
        memcpy(obj->data + offset, buf, size);
    } else {
        // No data yet, so allocate a new buffer.
        obj->data = calloc(1, size + 1);  // +1 for the null terminator
        if (!obj->data) return -ENOMEM;
        memcpy(obj->data, buf, size);
    }

    return size;
}

static int fuse_example_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    printf("fuse_example_create called with path: %s\n", path);
    
    int parent_inode = lookup_inode(dirname(strdup(path)));
    if (parent_inode < 0) return -ENOENT;

    if (lookup_inode(path) >= 0) return -EEXIST;

    // Allocate a new fs_object.
    num_fs_objects++;
    fs_objects = realloc(fs_objects, num_fs_objects * sizeof(fs_object));
    if (!fs_objects) return -ENOMEM;

    // Initialize the new fs_object.
    fs_object *new_obj = &fs_objects[num_fs_objects - 1];
    memset(new_obj, 0, sizeof(fs_object));
    new_obj->inode = num_fs_objects - 1;  // Assume that inodes are allocated sequentially.
    new_obj->type = "reg";
	char *temp_path = strdup(path);
    new_obj->name = strdup(basename(temp_path));  // The name is only the last part of the path.
	free (temp_path);
    if (!new_obj->name) return -ENOMEM;
    new_obj->data = NULL;  // Initially, the file has no data.
    new_obj->entries = NULL;  // Since it's not a directory, entries is NULL.

    // Add the new file to its parent directory.
    fs_object *parent_obj = &fs_objects[parent_inode];
    struct json_object *entry_obj = json_object_new_object();
    json_object_object_add(entry_obj, "name", json_object_new_string(new_obj->name));
    json_object_object_add(entry_obj, "inode", json_object_new_int(new_obj->inode));
    json_object_array_add(parent_obj->entries, entry_obj);

    // Open the new file.
    fi->fh = new_obj->inode;

    printf("fuse_example_create returning: %d\n", 0);
    return 0;
}

// Modified getattr function
static int fuse_example_getattr(const char *path, struct stat *stbuf) {
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));
    if(strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        int inode = lookup_inode(path);
        if (inode < 0) return -ENOENT;

        fs_object *obj = &fs_objects[inode];
        if (strcmp(obj->type, "reg") == 0) {
            stbuf->st_mode = S_IFREG | 0666;
            stbuf->st_nlink = 1;
            stbuf->st_size = obj->data ? strlen(obj->data) : 0;
        } else if (strcmp(obj->type, "dir") == 0) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
        } else {
            res = -ENOENT;
        }
    }

    return res;
}


static int fuse_example_truncate(const char *path, off_t newsize) {
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

    fs_object *obj = &fs_objects[inode];
    if (obj->data) {
        // Resize the data.
        obj->data = realloc(obj->data, newsize + 1);  // +1 for the null terminator
        if (!obj->data) return -ENOMEM;
        if (newsize > strlen(obj->data)) {
            memset(obj->data + strlen(obj->data), 0, newsize - strlen(obj->data));
        }
        obj->data[newsize] = '\0';
    } else {
        // No data yet, so allocate a new buffer.
        obj->data = calloc(1, newsize + 1);  // +1 for the null terminator
        if (!obj->data) return -ENOMEM;
    }

    return 0;
}

static int fuse_example_utimens(const char *path, const struct timespec tv[2]) {
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

    // In a real filesystem, we would update the timestamps in the inode here.
    // However, since we're not keeping track of timestamps in this example, we'll just do nothing.

    return 0;
}

static int fuse_example_mkdir(const char *path, mode_t mode) {
    printf("fuse_example_mkdir called with path: %s\n", path);
    
    if (lookup_inode(path) >= 0) return -EEXIST; // Directory already exists

    // Allocate a new fs_object.
    num_fs_objects++;
    fs_objects = realloc(fs_objects, num_fs_objects * sizeof(fs_object));
    if (!fs_objects) return -ENOMEM; // Not enough memory

    // Initialize the new fs_object.
    fs_object *new_obj = &fs_objects[num_fs_objects - 1];
    memset(new_obj, 0, sizeof(fs_object));
    new_obj->inode = num_fs_objects - 1;  // Assume that inodes are allocated sequentially.
    new_obj->type = "dir";
    new_obj->name = strdup(path + 1);  // Skip the leading slash
    if (!new_obj->name) return -ENOMEM; // Not enough memory
    new_obj->data = NULL;  // Since it's a directory, there's no data.
    new_obj->entries = json_object_new_array();  // Create an empty array of entries.

    // Add the new directory to the root directory.
    fs_object *root_obj = &fs_objects[0];
    struct json_object *entry_obj = json_object_new_object();
    json_object_object_add(entry_obj, "name", json_object_new_string(new_obj->name));
    json_object_object_add(entry_obj, "inode", json_object_new_int(new_obj->inode));
    json_object_array_add(root_obj->entries, entry_obj);

    printf("fuse_example_mkdir returning: %d\n", 0);
    return 0;
}


static struct fuse_operations fuse_example_oper = {
    .getattr = fuse_example_getattr,
    .open = fuse_example_open,
    .read = fuse_example_read,
    .readdir = fuse_example_readdir,
	.write = fuse_example_write,
	.create = fuse_example_create,
    .truncate = fuse_example_truncate,
    .utimens = fuse_example_utimens,
    .mkdir = fuse_example_mkdir,
};



int main(int argc, char *argv[]) {
    load_json_fs("fs.json");
    return fuse_main(argc, argv, &fuse_example_oper, NULL);
}

