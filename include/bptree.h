#ifndef BPTREE_H
#define BPTREE_H 

#ifdef __cplusplus
extern "C" {
#endif

/*
[Parent Node]
│
├── children[0] → [Child Node 0]
│                 ├── Node Header (is_leaf, key_count, etc.)
│                 ├── Key0
│                 ├── Key1
│                 ├── ... 
│                 └── [Child of Child pointers...]
│
├── children[1] → [Child Node 1]
│                 ├── Node Header
│                 ├── Key0
│                 └── ...
│
└── children[2] → [Child Node 2]
                    ├── Node Header
                    └── ...
*/

#ifndef BPTREE_STATIC // macro can be defined via compiler flag
#ifdef _WIN32 // it define automatiquely when its windows
#define BPTREE_API __declspec(dllexport) // Export symbol from DLL (Windows)
#else
#define BPTREE_API __attribute__((visibility("default"))) // Export symbol from shared library (Linux/macOS)
#endif
#else
#define BPTREE_API static // internal linkage, functions and variables only visible in this file
#endif

#include <assert.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#ifndef BPTREE_KEY_TYPE_STRING
#ifndef BPTREE_KEY_SIZE
#error "Define BPTREE_KEY_SIZE for fixed size string keys"
#endif

typedef struct {
    char data[BPTREE_KEY_SIZE];
} bptree_key_t;
#else // IF BPTREE_KEY_TYPE_STRING is not defined
#ifndef BPTREE_NUMERIC_TYPE
#define BPTREE_NUMERIC_TYPE int64_t
#endif

typedef BPTREE_NUMERIC_TYPE bptree_key_t;
#endif

#ifndef BPTREE_VALUE_TYPE
#define BPTREE_VALUE_TYPE void*
#endif


typedef BPTREE_VALUE_TYPE bptree_value_t;

typedef enum { // STATUS CODE RETURNED BY B+TREE FUNCTIONS
    BPTREE_OK = 0, // operation succeded
    BPTREE_DUPLICATE_KEY, // duplicate key found
    BPTREE_KEY_NOT_FOUND, // key not found
    BPTREE_ALLOCATION_FAILURE, // memory allocation failure
    BPTREE_INVALID_ARGUMENT, // invalid argument passed
    BPTREE_INTERNAL_ERROR // internal consistency error
} bptree_status;

typedef struct bptree_node bptree_node;
struct bptree_node {
    bool is_leaf; // if node is leaf return true
    int num_key; // number of keys stored in the node
    bptree_node* next; // pointer to the next leaf (range querie)
    char data[]; // flexible array member that holds keys and either values or child pointers
};

typedef struct bptree {
    int count; // total number of key/value pair in the tree
    int height; // current height of the tree
    bool enable_debug; // if true debug message will be printed
    int max_keys;   // maximum keys allowed in node
    int min_leaf_keys;  // minimum keys nedded in a non root leaf node
    int min_internal_keys; // minimum keys nedded in a non root internal node
    bptree_node* root; // pointer to the root node of the tree
} bptree;

typedef struct bptree_stats {
    int count;
    int height;
    int node_count;
} bptree_stats;

BPTREE_API bptree* bptree_create(int max_keys,
                                 int (*compare)(const bptree_key_t*, const bptree_key_t*), // a function pointer, custom key comparison
                                 bool enable_debug);


BPTREE_API void bptree_free(bptree* tree);

BPTREE_API bptree_status bptree_put(bptree* tree, const bptree_key_t* key, bptree_value_t value); // using const in key to ensure that the content of key which is value will not be modified

BPTREE_API bptree_status bptree_get(const bptree* tree, const bptree_key_t* key, bptree_value_t* out);

BPTREE_API bptree_status bptree_remove(bptree* tree, const bptree_key_t* key);

BPTREE_API bptree_status bptree_get_range(const bptree* tree, const bptree* start, const bptree* end, bptree_value_t** out_values, int* n_results); // bptree_value_t** out_values: c cant return an array directly so it's a pointer to array which is a pointer

BPTREE_API void bptree_free_range_results(bptree_value_t* results); // free the the out_values in bptree_get_range

BPTREE_API bptree_stats bptree_get_stats(const bptree* tree); // stat of the tree which is predefined

BPTREE_API bool bptree_check_invariants(const bptree* tree); // check the tree constraintes predefined and return true or false

BPTREE_API bool bptree_contains(const bptree* tree, const bptree_key_t* key); // check if the tree already contain the key

#ifdef BPTREE_IMPLEMENTATION // to implement the tree not only read

static void bptree_debug_print(const bool enable, const char* fmt, ...) {
    if (!enable) return;
    char time_buf[64];
    const time_t now = time(NULL); // time(NULL) return the current time
    const struct tm* tm_info = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("[%s] [BPTREE DEBUG] ", time_buf);
    va_list args; // variable that contain the arguments passed
    va_start(args, fmt); // initialize args to point to arguments after fmt
    vprintf(fmt, args); // same as printf but take va_list instead of separated variables
    va_end(args); // clean up the va_list
}

static size_t bptree_keys_area_size(const int max_keys) {
    const size_t keys_size = (size_t)(max_keys + 1) * sizeof(bptree_key_t); // bptree offten store one extra key temporary for splitting, size_t unsigned integer for size
    const size_t req_align = (sizeof(bptree_value_t) > sizeof(bptree_node*) ? sizeof(bptree_value_t) : sizeof(bptree_node*));

    // calculate required padding to meet alignement constrainte
    const size_t pad = (req_align - (keys_size % req_align)) % req_align;
    return keys_size + pad;
}


// return the pointer to the array of keys that the node contain
static bptree_key_t* bptree_node_keys(const bptree_node* node) { return (bptree_key_t*)node->data; }


// return the value stored in a node for leaf nodes
static bptree_value_t* bptree_node_values(bptree_node* node, const int max_keys) {
    const size_t offset = bptree_keys_area_size(max_keys); // how many bytes occuped bu keys in data[] in node
    return (bptree_value_t*)(node->data + offset); // skip the keys and retrieve the value
}

// it retrive the pointer to the first element in children nodes array
static bptree_node** bptree_node_children(bptree_node* node, const int max_keys) {
    const size_t offset = bptree_keys_area_size(max_keys);
    return (bptree**)(node->data + offset)
}

#ifdef BPTREE_KEY_TYPE_STRING

// comparing two keys as fixed-size strings
static inline int bptree_default_compare(const bptree_key_t* a, const bptree_key_t* b) {
    return memcmp(a->data, b->data, BPTREE_KEY_SIZE);
}

#else

// comparing two numeric keys
static int bptree_default_compare(const bptree_key_t* a, const bptree_key_t* b) {
    return (*a < *b) ? -1 : ((*a > *b) ? 1 : 0);
}

#endif
















}
#endif