#include "tree.h"
#include "pes.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Forward declarations for functions in object.c
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── HELPER ──────────────────────────────────────────────────────────────────

// Comparison function to ensure alphabetical order by name.
static int compare_tree_entries(const void *a, const void *b) {
    const TreeEntry *ea = (const TreeEntry *)a;
    const TreeEntry *eb = (const TreeEntry *)b;
    return strcmp(ea->name, eb->name);
}

// ─── PROVIDED FUNCTIONS ──────────────────────────────────────────────────────

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + len;
    tree_out->count = 0;

    while (p < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *e = &tree_out->entries[tree_out->count++];
        char type_name[16];
        char hex[HASH_HEX_SIZE + 1];
        int n;
        // Format: "<mode> <type> <hash-hex> <name>\n"
        if (sscanf((const char *)p, "%o %15s %64s %511[^\n]\n%n",
                   &e->mode, type_name, hex, e->name, &n) != 4) return -1;
        if (hex_to_hash(hex, &e->hash) != 0) return -1;
        p += n;
    }
    return 0;
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // We sort a temporary copy to ensure deterministic hashing.
    Tree sorted = *tree;
    qsort(sorted.entries, sorted.count, sizeof(TreeEntry), compare_tree_entries);

    char *buf = malloc(MAX_TREE_ENTRIES * 600); 
    if (!buf) return -1;

    int pos = 0;
    for (int i = 0; i < sorted.count; i++) {
        const TreeEntry *e = &sorted.entries[i];
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hex);

        const char *type_name = S_ISDIR(e->mode) ? "tree" : "blob";
        // Ensure format matches the parser: mode (octal), type, hex-hash, name
        pos += sprintf(buf + pos, "%o %s %s %s\n",
                       e->mode, type_name, hex, e->name);
    }

    *data_out = buf;
    *len_out = (size_t)pos;
    return 0;
}

// ─── TODO IMPLEMENTATION ─────────────────────────────────────────────────────

int tree_from_index(ObjectID *tree_id_out) {
    Index index;
    // Load staged files from the index.
    if (index_load(&index) != 0) return -1;

    Tree root_tree = { .count = 0 };

    // Group files from index into the tree structure.
    for (int i = 0; i < index.count; i++) {
        if (root_tree.count >= MAX_TREE_ENTRIES) break;

        TreeEntry *e = &root_tree.entries[root_tree.count++];
        e->mode = index.entries[i].mode;
        e->hash = index.entries[i].hash;
        strncpy(e->name, index.entries[i].path, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
    }

    // Convert the Tree struct into the binary buffer format.
    void *data;
    size_t len;
    if (tree_serialize(&root_tree, &data, &len) != 0) return -1;

    // Write the serialized tree object to the store.
    int rc = object_write(OBJ_TREE, data, len, tree_id_out);
    
    free(data); 
    return rc;
}
