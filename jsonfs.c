#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>

typedef struct {
    int inode;
    const char *type;
    const char *name;
    struct json_object *entries;
} fs_object;

static fs_object *fs_objects;
static int num_fs_objects;

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
        if (json_object_object_get_ex(obj, "name", &tmp))
            fs_objects[i].name = json_object_get_string(tmp);
        if (json_object_object_get_ex(obj, "entries", &tmp))
            fs_objects[i].entries = tmp;

        printf("Loaded fs_object: inode=%d, type=%s, name=%s\n",
               fs_objects[i].inode, fs_objects[i].type, fs_objects[i].name);
    }
}


static fs_object *find_fs_object_by_path(const char *path) {
    if (strcmp(path, "/") == 0) {
        return &fs_objects[0];  // inode 0 is the root directory
    }
    for (int i = 1; i < num_fs_objects; i++) { // start from 1 as we have handled root case
        struct json_object *entry;
        int num_entries = json_object_array_length(fs_objects[i].entries);
        for (int j = 0; j < num_entries; j++) {
            entry = json_object_array_get_idx(fs_objects[i].entries, j);
            if (strcmp(json_object_get_string(json_object_object_get_ex(entry, "name")), path + 1) == 0) {
                return &fs_objects[i];
            }
        }
    }
    fprintf(stderr, "Error: path not found: %s\n", path);
    return NULL; // return NULL instead of exiting
}

static int getattr_callback(const char *path, struct stat *stbuf)
{
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));
    fs_object *fs_obj = find_fs_object_by_path(path);
    if (!fs_obj) {
        return -ENOENT; // No such file or directory
    }

    if (strcmp(fs_obj->type, "dir") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(fs_obj->type, "reg") == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(fs_obj->data); // assuming 'data' is a string in your fs_object
    } else {
        res = -ENOENT; // No such file or directory
    }

    return res;
}



static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi) {
    fs_object *dir = find_fs_object_by_path(path);
    if (!dir || strcmp(dir->type, "dir") != 0) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    int num_entries = json_object_array_length(dir->entries);
    for (int i = 0; i < num_entries; i++) {
        struct json_object *entry = json_object_array_get_idx(dir->entries, i);
        struct json_object *tmp;
        if (json_object_object_get_ex(entry, "name", &tmp)) {
            const char *entry_name = json_object_get_string(tmp);
            filler(buf, entry_name, NULL, 0);
        }
    }

    return 0;
}

// Other callbacks...

static struct fuse_operations fs_oper = {
    .getattr = getattr_callback,
    .readdir = readdir_callback,
    // Other operation callbacks...
};

int main(int argc, char *argv[]) {
    load_json_fs("fs.json");
    return fuse_main(argc, argv, &fs_oper, NULL);
}

