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
        if (json_object_get_int(json_object_get(node, "inode")) == inode) {
            return node;
        }
    }
    return NULL;
}

struct jsonfs_file {
	int inode;
	char*content;
}

struct jsonfs_dir {
	int inode;
	char *entries[MAX_ENTRIES];
	int num_entries;
}

struct jsonfs {
    struct jsonfs_file *files[MAX_FILES];
    struct jsonfs_dir *dirs[MAX_FILES];
    int num_files;
    int num_dirs;
};

struct jsonfs* load_jsonfs(const char* path) {
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *string = malloc(fsize + 1);
    fread(string, 1, fsize, f);
    fclose(f);
    string[fsize] = 0;

    cJSON *root = cJSON_Parse(string);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        free(string);
        return NULL;
    }

    struct jsonfs* fs = malloc(sizeof(struct jsonfs));
    fs->num_files = 0;
    fs->num_dirs = 0;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (cJSON_IsObject(item)) {
            struct jsonfs_dir *dir = malloc(sizeof(struct jsonfs_dir));
            dir->inode = fs->num_dirs;
            dir->num_entries = 0;
            fs->dirs[fs->num_dirs++] = dir;

            cJSON *entries = cJSON_GetObjectItemCaseSensitive(item, "entries");
            cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, entries) {
                dir->entries[dir->num_entries++] = strdup(cJSON_GetStringValue(entry));
            }
        } else if (cJSON_IsString(item)) {
            struct jsonfs_file *file = malloc(sizeof(struct jsonfs_file));
            file->inode = fs->num_files;
            file->content = strdup(cJSON_GetStringValue(item));
            fs->files[fs->num_files++] = file;
        }
    }

    cJSON_Delete(root);
    free(string);
    return fs;
}

static int jsonfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        return -ENOENT;
    }
    
    const char* type = json_object_get_string(json_object_get(node, "type"));
    int size = json_object_get_int(json_object_get(node, "size"));

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
    
    const char* type = json_object_get_string(json_object_get(node, "type"));

    if (strcmp(type, "dir") == 0) {
        struct json_object* entries = json_object_get(node, "entries");
        int entries_len = json_object_array_length(entries);
        
        for (int i = 0; i < entries_len; i++) {
            struct json_object* entry = json_object_array_get_idx(entries, i);
            const char* name = json_object_get_string(json_object_get(entry, "name"));
            filler(buf, name, NULL, 0);
        }
    } else {
        return -EISDIR;
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

    const char* type = json_object_get_string(json_object_get(node, "type"));

    if (strcmp(type, "reg") == 0) {
    } else {
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

    const char* type = json_object_get_string(json_object_get(node, "type"));
    const char* data = json_object_get_string(json_object_get(node, "data"));

    if (strcmp(type, "reg") == 0) {
        size_t len = strlen(data);
        if (offset < len) {
            if (offset + size > len)
                size = len - offset;
            memcpy(buf, data + offset, size);
        } else
            size = 0;

        return size;
    } else {
        return -EISDIR;
    }

    return 0;
}

static int jsonfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        return -ENOENT;
    }

    const char* type = json_object_get_string(json_object_get(node, "type"));
    struct json_object* data_obj = json_object_get(node, "data");
    char* data = (char*)json_object_get_string(data_obj);

    if (strcmp(type, "reg") == 0) {
        size_t old_len = strlen(data);
        size_t new_len = offset + size;
        if (new_len > old_len) {
            data = realloc(data, new_len + 1);
            json_object_set_string_len(data_obj, data, new_len);
        }

        memcpy(data + offset, buf, size);
        data[new_len] = '\0';  // 항상 null로 끝나야 합니다.
        return size;
    } else {
        return -EISDIR;
    }

    return 0;
}

static int jsonfs_mkdir(const char *path, mode_t mode) {
    int parent_inode;
    char new_dir_name[256];
    sscanf(path, "/%d/%255s", &parent_inode, new_dir_name);

    struct json_object* parent_node = find_inode(parent_inode);
    if (parent_node == NULL) {
        return -ENOENT;
    }

    struct json_object* entries_obj = json_object_get(parent_node, "entries");

    int new_inode = json_object_array_length(fs_json);

    struct json_object* new_dir = json_object_new_object();
    json_object_object_add(new_dir, "inode", json_object_new_int(new_inode));
    json_object_object_add(new_dir, "type", json_object_new_string("dir"));
    json_object_object_add(new_dir, "entries", json_object_new_array());
    json_object_array_add(fs_json, new_dir);

    struct json_object* new_entry = json_object_new_object();
    json_object_object_add(new_entry, "name", json_object_new_string(new_dir_name));
    json_object_object_add(new_entry, "inode", json_object_new_int(new_inode));
    json_object_array_add(entries_obj, new_entry);

    return 0;
}

static int jsonfs_unlink(const char *path) {
    int parent_inode;
    char target_name[256];
    sscanf(path, "/%d/%255s", &parent_inode, target_name);

    struct json_object* parent_node = find_inode(parent_inode);
    if (parent_node == NULL) {
        return -ENOENT;
    }

    struct json_object* entries_obj = json_object_get(parent_node, "entries");
    int array_len = json_object_array_length(entries_obj);
    
    for (int i = 0; i < array_len; i++) {
        struct json_object* entry = json_object_array_get_idx(entries_obj, i);
        if (strcmp(json_object_get_string(json_object_get(entry, "name")), target_name) == 0) {
            json_object_array_put_idx(entries_obj, i, NULL);

            // 파일 시스템에서 해당 inode의 파일을 제거합니다.
            struct json_object* fs_obj = find_inode(json_object_get_int(json_object_get(entry, "inode")));
            if (fs_obj != NULL) {
                json_object_put(fs_obj);  // 이 함수는 json-c에서 제공하는 함수로 JSON 객체를 제거합니다.
            }

            return 0;
        }
    }

    return -ENOENT;
}

static int jsonfs_rmdir(const char *path) {
    int parent_inode;
    char target_name[256];
    sscanf(path, "/%d/%255s", &parent_inode, target_name);

    struct json_object* parent_node = find_inode(parent_inode);
    if (parent_node == NULL) {
        return -ENOENT;
    }

    int array_len = json_object_array_length(entries_obj);
    
    for (int i = 0; i < array_len; i++) {
        struct json_object* entry = json_object_array_get_idx(entries_obj, i);
        if (strcmp(json_object_get_string(json_object_get(entry, "name")), target_name) == 0) {
            // 목표 디렉토리를 찾았다면 entries 배열에서 제거합니다.
            json_object_array_put_idx(entries_obj, i, NULL);

            // 파일 시스템에서 해당 inode의 디렉토리를 제거합니다.
            struct json_object* fs_obj = find_inode(json_object_get_int(json_object_get(entry, "inode")));
            if (fs_obj != NULL) {
                // 디렉토리 안에 있는 모든 파일/디렉토리도 제거합니다.
                struct json_object* fs_entries_obj = json_object_get(fs_obj, "entries");
                int fs_array_len = json_object_array_length(fs_entries_obj);
                for (int j = 0; j < fs_array_len; j++) {
                    struct json_object* fs_entry = json_object_array_get_idx(fs_entries_obj, j);
                    int fs_inode = json_object_get_int(json_object_get(fs_entry, "inode"));
                    struct json_object* fs_node = find_inode(fs_inode);
                    if (fs_node != NULL) {
                        json_object_put(fs_node);  // 이 함수는 json-c에서 제공하는 함수로 JSON 객체를 제거합니다.
                    }
                }
                json_object_put(fs_obj);  // 이 함수는 json-c에서 제공하는 함수로 JSON 객체를 제거합니다.
            }

            return 0;
        }
    }

    // 목표 디렉토리를 찾지 못했다면, ENOENT를 반환합니다.
    return -ENOENT;
}


void jsonfs_destroy(void *private_data) {
    json_object_to_file("fs.json", fs_json);
}

static struct fuse_operations jsonfs_oper = {
    .getattr    = jsonfs_getattr,
    .readdir    = jsonfs_readdir,
    .open       = jsonfs_open,
    .read       = jsonfs_read,
    .write      = jsonfs_write,
    .mkdir      = jsonfs_mkdir,
    .unlink     = jsonfs_unlink,
    .rmdir      = jsonfs_rmdir,
    .destroy    = jsonfs_destroy,
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [input JSON file]\n", argv[0]);
        return 1;
    }

    struct jsonfs* fs = load_jsonfs(argv[1]);
    if (!fs) {
        return 1;
    }

    return fuse_main(argc, argv, &jsonfs_oper, fs);
}
