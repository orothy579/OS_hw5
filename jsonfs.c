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
    }
}

static fs_object *find_fs_object_by_path(const char *path) {
    for (int i = 0; i < num_fs_objects; i++) {
        if (strcmp(fs_objects[i].name, path + 1) == 0) {
            return &fs_objects[i];
        }
    }
    fprintf(stderr, "Error: path not found: %s\n", path);
    exit(-ENOENT);
}

static int getattr_callback(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    fs_object *obj = find_fs_object_by_path(path);
    if (!obj) {
        return -ENOENT;
    }

    if (strcmp(obj->type, "dir") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(obj->type, "reg") == 0) {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        // Assume the size of a file is the length of its name
        stbuf->st_size = strlen(obj->name);
    }

    return 0;
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

