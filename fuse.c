#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <json-c/json.h>

#define MAX_FILES 128
#define MAX_FILE_SIZE 4098
#define MAX_ENTRIES 16

struct json_object *fs_json;

struct json_object* find_inode(int inode) {
    int array_len = json_object_array_length(fs_json);
    for (int i = 0; i < array_len; i++) {
        struct json_object* node = json_object_array_get_idx(fs_json, i);
        struct json_object* inode_obj;
        json_object_object_get_ex(node, "inode", &inode_obj);
        if (json_object_get_int(inode_obj) == inode) {
            return node;
        }
    }
    return NULL;
}

struct jsonfs {
    int inode;
    char* type;
    union {
        struct jsonfs_entry* entries;
        char* data;
    };
};

struct jsonfs_entry {
    char* name;
    int inode;
};

struct jsonfs* load_jsonfs(const char* path) {
    fs_json = json_object_from_file(path);
    if (fs_json == NULL) {
        fprintf(stderr, "Cannot open JSON file: %s\n", path);
        return NULL;
    }

    struct jsonfs* new_jsonfs = malloc(sizeof(struct jsonfs));
    struct json_object* tmp;
    
    if (json_object_object_get_ex(fs_json, "inode", &tmp))
        new_jsonfs->inode = json_object_get_int(tmp);
    
    if (json_object_object_get_ex(fs_json, "type", &tmp))
        new_jsonfs->type = strdup(json_object_get_string(tmp));
    
    if (strcmp(new_jsonfs->type, "dir") == 0) {
        if (json_object_object_get_ex(fs_json, "entries", &tmp)) {
            int entries_len = json_object_array_length(tmp);
            new_jsonfs->entries = malloc(entries_len * sizeof(struct jsonfs_entry));
            
            for (int i = 0; i < entries_len; i++) {
                struct json_object* entry_obj = json_object_array_get_idx(tmp, i);
                
                if (json_object_object_get_ex(entry_obj, "name", &tmp))
                    new_jsonfs->entries[i].name = strdup(json_object_get_string(tmp));
                
                if (json_object_object_get_ex(entry_obj, "inode", &tmp))
                    new_jsonfs->entries[i].inode = json_object_get_int(tmp);
            }
        }
    } else if (strcmp(new_jsonfs->type, "reg") == 0) {
        if (json_object_object_get_ex(fs_json, "data", &tmp))
            new_jsonfs->data = strdup(json_object_get_string(tmp));
    } else {
        fprintf(stderr, "Unknown type: %s\n", new_jsonfs->type);
        free(new_jsonfs->type);
        free(new_jsonfs);
        return NULL;
    }

    return new_jsonfs;
}

static int jsonfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        return -ENOENT;
    }

    struct json_object* type_obj;
    struct json_object* size_obj;
    json_object_object_get_ex(node, "type", &type_obj);
    json_object_object_get_ex(node, "size", &size_obj);
    const char* type = json_object_get_string(type_obj);
    int size = json_object_get_int(size_obj);

    if (strcmp(type, "dir") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(type, "reg") == 0) {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = size;
    } else {
        return -EINVAL;
    }

    return 0;
}

static int jsonfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        return -ENOENT;
    }

    struct json_object* type_obj;
    json_object_object_get_ex(node, "type", &type_obj);
    const char* type = json_object_get_string(type_obj);

    if (strcmp(type, "dir") != 0) {
        return -ENOTDIR;
    }

    struct json_object* entries_obj;
    json_object_object_get_ex(node, "entries", &entries_obj);
    if (entries_obj == NULL) {
        return -ENOENT;
    }

    int array_len = json_object_array_length(entries_obj);
    for (int i = 0; i < array_len; i++) {
        struct json_object* entry = json_object_array_get_idx(entries_obj, i);
        struct json_object* name_obj;
        json_object_object_get_ex(entry, "name", &name_obj);
        const char* name = json_object_get_string(name_obj);
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = i;
        filler(buf, name, &st, 0);
    }

    return 0;
}

static int jsonfs_open(const char *path, struct fuse_file_info *fi) {
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        return -ENOENT;
    }

    struct json_object* type_obj;
    json_object_object_get_ex(node, "type", &type_obj);
    const char* type = json_object_get_string(type_obj);

    if (strcmp(type, "reg") != 0) {
        return -EISDIR;
    }

    return 0;
}

static int jsonfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        return -ENOENT;
    }

    struct json_object* type_obj;
    struct json_object* data_obj;
    json_object_object_get_ex(node, "type", &type_obj);
    json_object_object_get_ex(node, "data", &data_obj);
    const char* type = json_object_get_string(type_obj);
    const char* data = json_object_get_string(data_obj);

    if (strcmp(type, "reg") != 0) {
        return -EISDIR;
    }

    size_t len = strlen(data);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, data + offset, size);
    } else
        size = 0;

    return size;
}

static struct fuse_operations jsonfs_oper = {
    .getattr    = jsonfs_getattr,
    .readdir    = jsonfs_readdir,
    .open       = jsonfs_open,
    .read       = jsonfs_read,
};

int main(int argc, char *argv[]) {
    load_jsonfs("fs.json");

    return fuse_main(argc, argv, &jsonfs_oper, NULL);
}

