#define FUSE_USE_VERSION 26

#include <stdbool.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>

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


static int fuse_example_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

    const fs_object *obj = &fs_objects[inode];
    print_fs_object(obj);  
    // check path information
    if (strcmp(obj->type, "reg") == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = obj->data ? strlen(obj->data) : 0;
    } else if (strcmp(obj->type, "dir") == 0) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 2;
    } else {
        return -ENOENT;  // No such file or directory
    }

    return 0;
}

static int fuse_example_open(const char *path, struct fuse_file_info *fi) {
    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;  // No such file or directory

    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;  // Access denied
    }

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


static struct fuse_operations fuse_example_oper = {
    .getattr = fuse_example_getattr,
    .open = fuse_example_open,
    .read = fuse_example_read,
    .readdir = fuse_example_readdir,
};

int main(int argc, char *argv[]) {
    load_json_fs("fs.json");
    return fuse_main(argc, argv, &fuse_example_oper, NULL);
}

