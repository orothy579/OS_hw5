#define FUSE_USE_VERSION 26

#include <stdbool.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <libgen.h>
#include <pthread.h>

#define MAX_TEXT_SIZE 4096
#define MAX_ENTRIES_PER_DIR 16
#define MAX_FILES 128

#define MAX_FS_OBJECTS 4096
int max_fs_objects = 4096;
// We will use a dynamic array to hold our free inodes.
int *free_inodes = NULL;
int num_free_inodes = 0;

static int num_fs_objects;

pthread_mutex_t fs_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_free_inode(int inode) {
    // Increase the size of the free_inodes array.
    free_inodes = realloc(free_inodes, (num_free_inodes + 1) * sizeof(int));
    // Add the inode to the end of the array.
    free_inodes[num_free_inodes] = inode;
    num_free_inodes++;
}

int get_free_inode() {
    if (num_free_inodes == 0) {
        // If there are no free inodes, we just use the next available number.
        return num_fs_objects++;
    } else {
        // Otherwise, we remove the last element from the free_inodes array and return it.
        int inode = free_inodes[num_free_inodes - 1];
        free_inodes = realloc(free_inodes, (num_free_inodes - 1) * sizeof(int));
        num_free_inodes--;
        return inode;
    }
}


typedef struct {
    int inode;
    const char *type;
    char *name;
    char *data;
    struct json_object *entries;
} fs_object;

static fs_object *fs_objects;

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
    if(num_fs_objects > MAX_FILES){
        fprintf(stderr, "Too many files in the system\n");
        exit(1);
    }
    
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
            if(strlen(data) > MAX_TEXT_SIZE){
                fprintf(stderr, "File content size exceeds limit\n");
                exit(1);
            }
            fs_objects[i].data = strdup(data);
        } else {
            fs_objects[i].data = NULL;
        }
        if (json_object_object_get_ex(obj, "entries", &tmp)){
            fs_objects[i].entries = tmp;
            if(json_object_array_length(tmp) > MAX_ENTRIES_PER_DIR){
                fprintf(stderr, "Too many files in a directory\n");
                exit(1);
            }
        }

        print_fs_object(&fs_objects[i]);
    }
}

void initialize_file_system(const char *json_file) {
	pthread_mutex_lock(&fs_mutex);
    struct json_object *root_obj = json_object_from_file(json_file);
    if (!root_obj) {
        printf("Failed to load file system from %s\n", json_file);
        exit(1);
    }

    int array_length = json_object_array_length(root_obj);

    for (int i = 0; i < array_length; i++) {
        struct json_object *fs_object_json = json_object_array_get_idx(root_obj, i);

        struct json_object *inode_obj;
        struct json_object *type_obj;
        struct json_object *name_obj;
        struct json_object *data_obj;
        struct json_object *entries_obj;

        json_object_object_get_ex(fs_object_json, "inode", &inode_obj);
        json_object_object_get_ex(fs_object_json, "type", &type_obj);
        json_object_object_get_ex(fs_object_json, "name", &name_obj);
        json_object_object_get_ex(fs_object_json, "data", &data_obj);
        json_object_object_get_ex(fs_object_json, "entries", &entries_obj);

        fs_objects[i].inode = json_object_get_int(inode_obj);
        fs_objects[i].type = strdup(json_object_get_string(type_obj));
        fs_objects[i].name = name_obj ? strdup(json_object_get_string(name_obj)) : NULL;
        fs_objects[i].data = data_obj ? strdup(json_object_get_string(data_obj)) : NULL;
        fs_objects[i].entries = entries_obj ? json_object_get(entries_obj) : NULL;
    }
	pthread_mutex_unlock(&fs_mutex);
    json_object_put(root_obj);
}

void store_file_system(char *json_file) {
	pthread_mutex_lock(&fs_mutex);
    // Initialize a new JSON array object
    struct json_object *root_obj = json_object_new_array();
    
    // Iterate over all fs_objects
    for (int i = 0; i < MAX_FS_OBJECTS; i++) {
        // Check if the fs_object is in use (type is not NULL)
        if (fs_objects[i].type != NULL) {
            struct json_object *fs_obj = json_object_new_object();

            // Add other fields...
            json_object_object_add(fs_obj, "inode", json_object_new_int(fs_objects[i].inode));
            json_object_object_add(fs_obj, "type", json_object_new_string(fs_objects[i].type));
            json_object_object_add(fs_obj, "name", json_object_new_string(fs_objects[i].name));

            // If it's a regular file, add data
            if(strcmp(fs_objects[i].type, "reg") == 0) {
                json_object_object_add(fs_obj, "data", json_object_new_string(fs_objects[i].data));
            }

            // If it's a directory, add entries
            if(strcmp(fs_objects[i].type, "dir") == 0) {
                struct json_object *entry_list = fs_objects[i].entries;
                json_object_object_add(fs_obj, "entries", json_object_get(entry_list)); // Increments the reference count of entry_list
            }

            json_object_array_add(root_obj, fs_obj);
        }
    }
    
    // Write the root_obj to the JSON file
    if (json_object_to_file_ext(json_file, root_obj, JSON_C_TO_STRING_PRETTY) != 0) {
        fprintf(stderr, "Failed to write JSON file: %s\n", json_file);
    }

	pthread_mutex_unlock(&fs_mutex);
    // Decrement the reference count of root_obj to free it
    json_object_put(root_obj);
}

static void *fuse_example_init(struct fuse_conn_info *conn) {
    (void) conn;
    initialize_file_system("fs.json");
    return NULL;
}

static void fuse_example_destroy(void *private_data) {
    (void) private_data;
    store_file_system("fs_edited.json");
}

static int lookup_inode(const char *path) {
    char *path_copy = strdup(path);
	if(!path_copy) {
		return -ENOMEM;
	}

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
	size_t  new_size = offset + size;
	if(new_size > MAX_TEXT_SIZE) return -EFBIG;
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
	pthread_mutex_lock(&fs_mutex);
	if(num_fs_objects >= MAX_FILES) return -EDQUOT;
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
    pthread_mutex_unlock(&fs_mutex);
	return 0;
}

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
    
    if (lookup_inode(path) >= 0){
		return -EEXIST; // Directory already exists
	}
	
    // Allocate a new fs_object.
    num_fs_objects++;
    fs_objects = realloc(fs_objects, num_fs_objects * sizeof(fs_object));
    if (!fs_objects){ 
		 return -ENOMEM; // Not enough memory
		}

    // Initialize the new fs_object.
    fs_object *new_obj = &fs_objects[num_fs_objects - 1];
    memset(new_obj, 0, sizeof(fs_object));
    new_obj->inode = num_fs_objects - 1;  // Assume that inodes are allocated sequentially.
    new_obj->type = "dir";
    new_obj->name = strdup(path + 1);  // Skip the leading slash
    if (!new_obj->name) return -ENOMEM; // Not enough memory
    new_obj->data = NULL;  // Since it's a directory, there's no data.
    new_obj->entries = json_object_new_array();  // Create an empty array of entries.

    // Find the parent directory.
    char *parent_path = strdup(path);
    dirname(parent_path);
    int parent_inode = lookup_inode(parent_path);
    free(parent_path);
    if (parent_inode < 0) return -ENOENT;  // Parent directory does not exist.
    fs_object *parent_obj = &fs_objects[parent_inode];

    // Add the new directory to the parent directory.
    struct json_object *entry_obj = json_object_new_object();
    json_object_object_add(entry_obj, "name", json_object_new_string(new_obj->name));
    json_object_object_add(entry_obj, "inode", json_object_new_int(new_obj->inode));
    json_object_array_add(parent_obj->entries, entry_obj);

    printf("fuse_example_mkdir returning: %d\n", 0);
    return 0;
}



static int fuse_example_unlink(const char *path) {
    printf("fuse_example_unlink called with path: %s\n", path);

    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;

    // If the fs_object is a directory and not empty, return -ENOTEMPTY
    if(strcmp(fs_objects[inode].type, "dir") == 0) {
        struct json_object *entry_list = fs_objects[inode].entries;
        int num_entries = json_object_array_length(entry_list);
        if(num_entries > 0) {
            return -ENOTEMPTY;
        }
    }

    // Free the memory for the file's name and data.
    free(fs_objects[inode].name);
    if (fs_objects[inode].data) free(fs_objects[inode].data);

    // Add the inode back to the free list
    add_free_inode(inode);

    // Reset fs_object fields
    fs_objects[inode].type = NULL;
    fs_objects[inode].name = NULL;
    fs_objects[inode].data = NULL;
    fs_objects[inode].entries = NULL;

    // Remove the entry for this file from its parent directory.
    char *parent_path = strdup(path);
    dirname(parent_path);
    int parent_inode = lookup_inode(parent_path);
    free(parent_path);
    if (parent_inode < 0) return -ENOENT;  // This should never happen.

    struct json_object *entry_list = fs_objects[parent_inode].entries;
    int num_entries = json_object_array_length(entry_list);
    for (int i = 0; i < num_entries; i++) {
        struct json_object *entry_obj = json_object_array_get_idx(entry_list, i);
        struct json_object *inode_obj;
        if (json_object_object_get_ex(entry_obj, "inode", &inode_obj)) {
            int entry_inode = json_object_get_int(inode_obj);
            if (entry_inode == inode) {
                // We've found the entry for the file we're deleting. Remove it.
                // Since there's no json_object_array_remove_idx, we have to create a new array without the deleted element.
                struct json_object *new_entry_list = json_object_new_array();
                for (int j = 0; j < num_entries; j++) {
                    if (j != i) {
                        json_object_array_add(new_entry_list, json_object_array_get_idx(entry_list, j));
                    }
                }
                json_object_put(entry_list);  // Decrement the reference count of the old entry_list so it gets freed.
                fs_objects[parent_inode].entries = new_entry_list;  // Use the new entry_list.
                break;
            }
        }
    }

    printf("fuse_example_unlink returning: %d\n", 0);
    return 0;
}

// Declare the json_object_array_splice() function.
extern void json_object_array_splice(struct json_object *array, int index, int num_elements);



static int fuse_example_rmdir(const char *path)
{
    printf("fuse_example_rmdir called with path: %s\n", path);

    int inode = lookup_inode(path);
    if (inode < 0) return -ENOENT;

    // If the fs_object is not a directory, return -ENOTDIR
    if(strcmp(fs_objects[inode].type, "dir") != 0) {
        return -ENOTDIR;
    }

    // If the directory is not empty, return -ENOTEMPTY
    struct json_object *entry_list = fs_objects[inode].entries;
    int num_entries = json_object_array_length(entry_list);
    if(num_entries > 0) {
        return -ENOTEMPTY;
    }

    // Free the memory for the directory's name.
    free(fs_objects[inode].name);

    // Mark this fs_object as free.
    fs_objects[inode].type = NULL;

    // Remove the entry for this directory from its parent directory.
    char *parent_path = strdup(path);
    dirname(parent_path);
    int parent_inode = lookup_inode(parent_path);
    free(parent_path);
    if (parent_inode < 0) return -ENOENT;  // This should never happen.

    struct json_object *parent_entry_list = fs_objects[parent_inode].entries;
    int parent_num_entries = json_object_array_length(parent_entry_list);
    for (int i = 0; i < parent_num_entries; i++) {
        struct json_object *entry_obj = json_object_array_get_idx(parent_entry_list, i);
        struct json_object *inode_obj;
        if (json_object_object_get_ex(entry_obj, "inode", &inode_obj)) {
            int entry_inode = json_object_get_int(inode_obj);
            if (entry_inode == inode) {
                // We've found the entry for the directory we're deleting. Remove it.
                // Since there's no json_object_array_remove_idx, we have to create a new array without the deleted element.
                struct json_object *new_entry_list = json_object_new_array();
                for (int j = 0; j < parent_num_entries; j++) {
                    if (j != i) {
                        json_object_array_add(new_entry_list, json_object_array_get_idx(parent_entry_list, j));
                    }
                }
                json_object_put(parent_entry_list);  // Decrement the reference count of the old entry_list so it gets freed.
                fs_objects[parent_inode].entries = new_entry_list;  // Use the new entry_list.
                break;
            }
        }
    }

    printf("fuse_example_rmdir returning: %d\n", 0);
    return 0;
}


static struct fuse_operations fuse_example_oper = {
    .init = fuse_example_init,
    .destroy = fuse_example_destroy,
    .getattr = fuse_example_getattr,
    .open = fuse_example_open,
    .read = fuse_example_read,
    .readdir = fuse_example_readdir,
	.truncate = fuse_example_truncate,
	.write = fuse_example_write,
	.create = fuse_example_create,
    .utimens = fuse_example_utimens,
    .mkdir = fuse_example_mkdir,
    .unlink = fuse_example_unlink,
	.rmdir = fuse_example_rmdir,
};



int main(int argc, char *argv[]) {
	pthread_mutex_init(&fs_mutex,NULL);
    load_json_fs("fs.json");
    return fuse_main(argc, argv, &fuse_example_oper, NULL);
}

