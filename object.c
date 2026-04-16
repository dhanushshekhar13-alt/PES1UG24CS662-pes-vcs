// object.c — Content-addressable object store
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <errno.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTATION ──────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // 1. Build the full object: header ("type size\0") + data
    char header[64];
    int header_len = sprintf(header, "%s %zu", type_str, len) + 1; // +1 for \0
    size_t full_len = header_len + len;
    uint8_t *full_obj = malloc(full_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    // 2. Compute SHA-256 hash of the FULL object
    compute_hash(full_obj, full_len, id_out);

    // 3. Check for deduplication
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    // 4. Create shard directory (.pes/objects/XX/)
    char path[512];
    object_path(id_out, path, sizeof(path));
    char dir_path[512];
    strncpy(dir_path, path, 512);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) *last_slash = '\0';
    mkdir(dir_path, 0755);

    // 5. Write to a temporary file
    char temp_path[520];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(full_obj);
        return -1;
    }

    if (write(fd, full_obj, full_len) != (ssize_t)full_len) {
        close(fd);
        free(full_obj);
        return -1;
    }

    // 6. fsync() and rename()
    fsync(fd);
    close(fd);
    free(full_obj);

    if (rename(temp_path, path) != 0) return -1;

    // 7. Sync shard directory to persist rename
    int dfd = open(dir_path, O_RDONLY);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    // 1. Open and read the entire file
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long full_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *full_obj = malloc(full_len);
    if (fread(full_obj, 1, full_len, f) != (size_t)full_len) {
        fclose(f);
        free(full_obj);
        return -1;
    }
    fclose(f);

    // 2. Verify integrity
    ObjectID actual_id;
    compute_hash(full_obj, full_len, &actual_id);
    if (memcmp(id->hash, actual_id.hash, HASH_SIZE) != 0) {
        free(full_obj);
        return -1;
    }

    // 3. Parse the header
    char *header = (char *)full_obj;
    char *null_byte = memchr(full_obj, '\0', full_len);
    if (!null_byte) {
        free(full_obj);
        return -1;
    }

    char type_str[16];
    size_t size;
    if (sscanf(header, "%15s %zu", type_str, &size) != 2) {
        free(full_obj);
        return -1;
    }

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;
    else {
        free(full_obj);
        return -1;
    }

    // 4. Allocate and copy the data portion
    *len_out = size;
    *data_out = malloc(size);
    memcpy(*data_out, null_byte + 1, size);

    free(full_obj);
    return 0;
}
