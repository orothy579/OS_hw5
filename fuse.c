#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <json-c/json.h>

// 파일 시스템 정보를 저장하는 전역 JSON 객체
struct json_object *fs_json;

// JSON 객체에서 특정 inode를 가진 노드를 찾습니다.
struct json_object* find_inode(int inode) {
    int array_len = json_object_array_length(fs_json);
    for (int i = 0; i < array_len; i++) {
        struct json_object* node = json_object_array_get_idx(fs_json, i);
        if (json_object_get_int(json_object_object_get(node, "inode")) == inode) {
            return node;
        }
    }
    return NULL;
}

// FUSE getattr 콜백
    // 'path'에 대한 inode를 찾아야 함.
    // inode를 찾으면, find_inode 함수를 사용하여 JSON 객체를 가져올 수 있습니다.
    // 그런 다음 JSON 객체에서 파일 유형, 크기 등을 얻을 수 있습니다.
static int jsonfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    
    // 이 예에서는 경로에서 직접 inode 번호를 파싱하고 있습니다. 
    // 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        // inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }
    
    // JSON 객체에서 파일 메타데이터를 얻습니다.
    const char* type = json_object_get_string(json_object_object_get(node, "type"));
    int size = json_object_get_int(json_object_object_get(node, "size"));

    if (strcmp(type, "dir") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(type, "reg") == 0) {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = size;
    } else {
        // 알 수 없는 파일 유형은 오류를 반환합니다.
        return -EINVAL;
    }

    return 0;
}

// FUSE readdir 콜백
    // 'path'에 대한 inode를 찾아야 함.
    // inode를 찾으면, 해당 디렉토리의 모든 항목에 대해 filler 함수를 호출하여 디렉토리 항목을 추가할 수 있습니다.
static int jsonfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    // 이 예에서는 경로에서 직접 inode 번호를 파싱하고 있습니다. 
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        // inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }
    
    // JSON 객체에서 파일 유형을 얻습니다.
    const char* type = json_object_get_string(json_object_object_get(node, "type"));

    if (strcmp(type, "dir") == 0) {
        // 디렉터리의 경우, 그 안의 모든 항목을 채웁니다.
        struct json_object* entries = json_object_object_get(node, "entries");
        int entries_len = json_object_array_length(entries);
        
        for (int i = 0; i < entries_len; i++) {
            struct json_object* entry = json_object_array_get_idx(entries, i);
            const char* name = json_object_get_string(json_object_object_get(entry, "name"));
            filler(buf, name, NULL, 0);
        }
    } else {
        // 디렉터리가 아니면, EISDIR을 반환합니다.
        return -EISDIR;
    }

    return 0;
}

// FUSE open 콜백
static int jsonfs_open(const char *path, struct fuse_file_info *fi) {
    // 이 예에서는 경로에서 직접 inode 번호를 파싱하고 있습니다. 
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        // inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }

    // JSON 객체에서 파일 유형을 얻습니다.
    const char* type = json_object_get_string(json_object_object_get(node, "type"));

    if (strcmp(type, "reg") == 0) {
        // 파일의 경우, 일단 아무런 조치를 취하지 않습니다. 
        // 다만, 여기서 파일에 대한 추가적인 권한 검사 등을 수행할 수 있습니다.
    } else {
        // 파일이 아니면, EISDIR을 반환합니다.
        return -EISDIR;
    }

    return 0;
}

// FUSE read 콜백
static int jsonfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    // 이 예에서는 경로에서 직접 inode 번호를 파싱하고 있습니다. 
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        // inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }

    // JSON 객체에서 파일 유형과 데이터를 얻습니다.
    const char* type = json_object_get_string(json_object_object_get(node, "type"));
    const char* data = json_object_get_string(json_object_object_get(node, "data"));

    if (strcmp(type, "reg") == 0) {
        // 파일의 경우, 요청한 바이트 수만큼 데이터를 복사합니다.
        size_t len = strlen(data);
        if (offset < len) {
            if (offset + size > len)
                size = len - offset;
            memcpy(buf, data + offset, size);
        } else
            size = 0;

        return size;
    } else {
        // 파일이 아니면, EISDIR을 반환합니다.
        return -EISDIR;
    }

    return 0;
}

// FUSE write 콜백
static int jsonfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    // 이 예에서는 경로에서 직접 inode 번호를 파싱하고 있습니다.
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int inode;
    sscanf(path, "/%d", &inode);

    struct json_object* node = find_inode(inode);
    if (node == NULL) {
        // inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }

    // JSON 객체에서 파일 유형과 데이터를 얻습니다.
    const char* type = json_object_get_string(json_object_object_get(node, "type"));
    struct json_object* data_obj = json_object_object_get(node, "data");
    char* data = (char*)json_object_get_string(data_obj);

    if (strcmp(type, "reg") == 0) {
        // 파일의 경우, 'buf'의 내용을 파일의 'data' 필드에 쓸 수 있습니다.
        size_t old_len = strlen(data);
        size_t new_len = offset + size;
        if (new_len > old_len) {
            // 필요한 경우, 'data' 버퍼를 확장합니다.
            data = realloc(data, new_len + 1);
            json_object_set_string_len(data_obj, data, new_len);
        }

        // 'buf'의 내용을 파일의 'data' 필드에 씁니다.
        memcpy(data + offset, buf, size);
        data[new_len] = '\0';  // 항상 null로 끝나야 합니다.
        return size;
    } else {
        // 파일이 아니면, EISDIR을 반환합니다.
        return -EISDIR;
    }

    return 0;
}

// FUSE mkdir 콜백
static int jsonfs_mkdir(const char *path, mode_t mode) {
    // 이 예에서는 경로에서 부모 디렉토리의 inode 번호와 새 디렉토리의 이름을 파싱하고 있습니다.
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int parent_inode;
    char new_dir_name[256];
    sscanf(path, "/%d/%255s", &parent_inode, new_dir_name);

    struct json_object* parent_node = find_inode(parent_inode);
    if (parent_node == NULL) {
        // 부모 inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }

    // JSON 객체에서 부모 디렉토리의 entries를 얻습니다.
    struct json_object* entries_obj = json_object_object_get(parent_node, "entries");

    // 새로운 디렉토리의 inode 번호를 만듭니다.
    int new_inode = json_object_array_length(fs_json);

    // 새로운 디렉토리를 생성하고 fs_json에 추가합니다.
    struct json_object* new_dir = json_object_new_object();
    json_object_object_add(new_dir, "inode", json_object_new_int(new_inode));
    json_object_object_add(new_dir, "type", json_object_new_string("dir"));
    json_object_object_add(new_dir, "entries", json_object_new_array());
    json_object_array_add(fs_json, new_dir);

    // 부모 디렉토리의 entries에 새 디렉토리를 추가합니다.
    struct json_object* new_entry = json_object_new_object();
    json_object_object_add(new_entry, "name", json_object_new_string(new_dir_name));
    json_object_object_add(new_entry, "inode", json_object_new_int(new_inode));
    json_object_array_add(entries_obj, new_entry);

    return 0;
}

// FUSE unlink 콜백
static int jsonfs_unlink(const char *path) {
    // 이 예에서는 경로에서 부모 디렉토리의 inode 번호와 삭제할 파일의 이름을 파싱하고 있습니다.
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int parent_inode;
    char target_name[256];
    sscanf(path, "/%d/%255s", &parent_inode, target_name);

    struct json_object* parent_node = find_inode(parent_inode);
    if (parent_node == NULL) {
        // 부모 inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }

    // JSON 객체에서 부모 디렉토리의 entries를 얻습니다.
    struct json_object* entries_obj = json_object_object_get(parent_node, "entries");
    int array_len = json_object_array_length(entries_obj);
    
    // 목표 파일을 찾아서 제거합니다.
    for (int i = 0; i < array_len; i++) {
        struct json_object* entry = json_object_array_get_idx(entries_obj, i);
        if (strcmp(json_object_get_string(json_object_object_get(entry, "name")), target_name) == 0) {
            // 목표 파일을 찾았다면 entries 배열에서 제거합니다.
            json_object_array_put_idx(entries_obj, i, NULL);

            // 파일 시스템에서 해당 inode의 파일을 제거합니다.
            struct json_object* fs_obj = find_inode(json_object_get_int(json_object_object_get(entry, "inode")));
            if (fs_obj != NULL) {
                json_object_put(fs_obj);  // 이 함수는 json-c에서 제공하는 함수로 JSON 객체를 제거합니다.
            }

            return 0;
        }
    }

    // 목표 파일을 찾지 못했다면, ENOENT를 반환합니다.
    return -ENOENT;
}

// FUSE rmdir 콜백
static int jsonfs_rmdir(const char *path) {
    // 이 예에서는 경로에서 부모 디렉토리의 inode 번호와 삭제할 디렉토리의 이름을 파싱하고 있습니다.
    // 실제 구현에서는 복잡한 경로 구문 분석 및 이름-매핑 검색이 필요할 수 있습니다.
    int parent_inode;
    char target_name[256];
    sscanf(path, "/%d/%255s", &parent_inode, target_name);

    struct json_object* parent_node = find_inode(parent_inode);
    if (parent_node == NULL) {
        // 부모 inode가 없으면, ENOENT를 반환합니다.
        return -ENOENT;
    }

    // JSON 객체에서 부모 디렉토리의 entries를 얻습니다.
    struct json_object* entries_obj = json_object_object_get(parent_node, "entries");
    int array_len = json_object_array_length(entries_obj);
    
    // 목표 디렉토리를 찾아서 제거합니다.
    for (int i = 0; i < array_len; i++) {
        struct json_object* entry = json_object_array_get_idx(entries_obj, i);
        if (strcmp(json_object_get_string(json_object_object_get(entry, "name")), target_name) == 0) {
            // 목표 디렉토리를 찾았다면 entries 배열에서 제거합니다.
            json_object_array_put_idx(entries_obj, i, NULL);

            // 파일 시스템에서 해당 inode의 디렉토리를 제거합니다.
            struct json_object* fs_obj = find_inode(json_object_get_int(json_object_object_get(entry, "inode")));
            if (fs_obj != NULL) {
                json_object_put(fs_obj);  // 이 함수는 json-c에서 제공하는 함수로 JSON 객체를 제거합니다.
            }

            return 0;
        }
    }

    // 목표 디렉토리를 찾지 못했다면, ENOENT를 반환합니다.
    return -ENOENT;
}

// FUSE destroy 콜백
void jsonfs_destroy(void *private_data) {
    // 파일 시스템이 언마운트 될 때 JSON 파일로 상태를 저장합니다.
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

int main(int argc, char *argv[]) {
    fs_json = json_object_from_file("fs.json");
    assert(fs_json != NULL);
    return fuse_main(argc, argv, &jsonfs_oper, NULL);
}
