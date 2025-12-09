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

// finding the smallest key by going down in left
static bptree_key_t bptree_find_smallest_key(bptree_node* node, const int max_keys) {
    assert(node != NULL); // node is not null
    while (!node->is_leaf) { // while the node is an internal node
        assert(node->num_keys >= 0); // node is valid and has keys
        node = bptree_node_children(node, max_keys)[0]; // get childrens and affect the very left one to variable nod
        assert(node != NULL);
    }
    assert(node->num_key > 0); // check if the node is valid and has keys
    return bptree_node_keys(node)[0]; // return the very left key
}

static bptree_key_t bptree_find_largest_key(bptree_node* node, const int max_keys) {
    assert(node != NULL);
    while (!node->is_leaf) {
        assert(node->num_keys >= 0); // for simplicity 
        node = bptree_node_children(node)[node->num_keys] // num_keys + 1 because noode has one extra key so last element: the very right element
        assert(node != NULL);
    }
    assert(node->num_keys > 0);
    return bptree_node_keys(node)[node->num_keys - 1]; // the normal last element
}

static int bptree_count_nodes(const bptree_node* node, const bptree* tree) {
    if (!node) return 0;
    if (node->is_leaf) return 1;
    int count = 1;
    bptree_node** children = bptree_node_children((bptree_node*)node, tree->max_keys);


}

if (tree->compare(&max_in_child0, &keys[0]) >= 0) {
    // Fails if max_in_child0 >= keys[0]
    // This means equality (==) would FAIL!
}


/*
    the tree validator called after each interaction with the tree
    validate :
        structural properties (key ordering, node occupancy limites)
        relational properties (parent-child key relationships)
        tree properties (all leaves at same depth)
    it checks :
        sorted keys: keys within a node must be in ascending order
        occupancy constraints: nodes must have appropriate number of keys (min <= keys <= max)
        leaf depth uniformity: all leaves must be at the same depth
        parent-child relationships:
            all keys in child are <= key[i]
            all keys in child are > key[i-1]
        root special case: root has different occupancy rules
*/



static bool bptree_check_invariants_node(bptree_node* node, const bptree* tree, const int depth, int* leaf_depth) {
    if (!node) return false; // if node pointer is NULL, tree is invalide
    const ptree_key_t* keys = bptree_node_keys(node); // get pointer to the keys array
    const bool is_root = (tree->root == node); // check if the node is root

    // check that keys are in sorted order: increasing from left to right
    for(int i = 1; i < node->num_keys; i++) {
        if (tree->compare(&keys[i - 1], &keys[i]) >= 0) { // compare keys: previous key shuld be smaller that the key
            bptree_debug_print(tree->enable_debug, "Invariant Fail: Keys not sorted in node %p\n", (void*)node);
            return false;
        }
    }
    if (node->is_leaf) { // if it's a leaf node
        if (*leaf_depth == -1) { // remember that leaf_depth is a variable holder to hold the depth and compare it to all leaf nods and it is initialized to 0
            *leaf_depth = depth; // so if it's the first leaf_node encoutred we modify it to tree depth
        } else if (depth != *leaf_depth) { // if the depth of node is mismatched to the depth
            bptree_debug_print(tree->enable_debug, "Invariant Fail: Leaf depth mismatch (%d != %d) for node %p\n", depth, *leaf_depth, (void*)node);
            return false;
        }

        if (!is_root && (node->num_keys < tree->min_leaf_keys || node->num_keys > tree->max_keys)) { // check the keys count in a non-root node should be > min_leaf_keys and < max_keys
            bptree_debug_print(tree->enable_debug; "Invariant Fail: leaf node %p key count out of range [%d, %d] (%d keys)\n", (void*)node, tree->min_leaf_keys, tree->max_keys, node->num_keys);
            return false;
        }

        if (is_root && (node->num_keys > tree->max_keys && tree->count > 0)) { // special case: root leaf node - num keys > max keys
            bptree_debug_print(tree->enable_debug, "Invariant Fail: root leaf node %p key count > max_keys (%d > %d)\n", (void*)node, node->num_keys, tree->max_keys);
            return false;
        }

        if (is_root && tree->count == 0 && node->num_keys != 0) { // check when the tree is empty mean the tree-count is 0 if the root leaf node has key, it shouldn't
            bptree_debug_print(tree->enable_debug, "Invariant Fail: Empty tree root leaf %p has keys (%d)\n", (void*)node, node->num_keys);
            return false;  
        }

        // no mismatch or error detected
        return true;
    } else {
        // if it's an internal node, check occupancy constraint: occupancy are the keys occupying the node, constraints are the rules
        if (!is_root && (node->num_keys < tree->min_internal_keys || node->num_keys > tree->max_keys)) { // out of range case : n_key should be < max and > min
            bptree_debug_print(tree->enable_debug, "Invariant Fail: Internal node %p key count out of range [%d, %d] (%d keys)\n", (void*)node, tree->min_internal_keys, tree->max_keys, node->num_keys);
            return false;
        }

        if (is_root && tree->count > 0 && node->num_keys < 1) { // tree->count > 0 so tree has items node n_keys should be > 0 so it can be splitted
            bptree_debug_print(tree->enable_debug, "Invariant Fail: Internal root node %p has < 1 key (%d keys) in non-empty tree\n", (void*)node, node->num_keys);
            return false;
        }

        if (is_root && node->num_keys > tree->max_keys) { // root node can't exed max can't have 'k + 1' or 'k if we start from 0' elements
            bptree_debug_print(tree->enable_debug, "Invariant Fail: Internal root node %p has > max_keys (%d > %d)\n", (void*)node, node->num_keys, tree->max_keys);
            return false;
        }

        // get a pointer to the array of child pointers from internal node
        bptree_node** children = bptree_node_children(node, tree->max_keys);
        if (node->num_keys >= 0) {
            if (!children[0]) { // every internal node must have a left most child
                bptree_debug_print(tree->enable_debug, "Invariant Fail: internal node %p missing child[0]\n", (void*)node);
                return false;
            }

            if (node->num_keys > 0 && (children[0]->num_keys > 0 || !children[0]->is_leaf)) { // if the children is an anternal node that has keys
                const bptree_key_t max_in_child0 = bptree_find_largest_key(children[0], tree->max_keys); // largest key in child node
                if (tree->compare(&max_in_child0, &keys[0]) >= 0) { // if not : all the keys in child[0] left most child should be less than the left parent key
                    bptree_debug_print(tree->enable_debug, "Invariant Fail: max(child[0]) >= key[0] in node %p --" "MaxChild=%lld key=%lld\n", (void*)node, (long long)max_in_child0, (long long)keys[0]);
                    return false;
                }
            }

            if (!bptree_check_invariants_node(children[0], tree, depth + 1, leaf_depth))
                return false;

            // we didnt include children[0] in the loop because of the comparison difference, 
            // loop ober the remaining children
            for (int i = 1; i <= node->num_keys; i++) {
                if (!children[i]) { // childs must exist
                    bptree_debug_print(tree->enable_debug, "Invariant Fail: Internal node %p missing child[%d]\n", (void*)node, i);
                    return false;
                }

                if (children[i]->num_keys > 0 || !children[i]->is_leaf) { // children is an internal non-leaf or leaf and has keys node and has keys
                    bptree_key_t min_in_child = bptree_find_smallest_key(children[i], tree->max_keys); // smaller children keys
                    if (tree->compare(&keys[i - 1], &min_in_child) != 0) { // in a node the i - 1'th key must equal the i'th children's minimum key 
                        bptree_debug_print(tree->enable_debug, "Invariante Fail: key[%d] != min(child[%d]) in node %p\n", i-1, i, (void*)node);
                        return false;
                    }

                    if (i < node->num_keys) { // if the children is in between first and last childrens
                        bptree_key_t max_in_child = bptree_find_largest_key(children[i], tree->max_keys); // find the largest key in that children
                        if (tree->compare(&max_in_child, &keys[i]) >= 0) { // all children[i]'s keys must be less than key[i]
                            bptree_debug_print(tree->enable_debug, "Invariant Fail: max(child[%d]) >= key[%d] in node %p\n", i, i, (void*)node);
                            return false;
                        }
                    }
                } else if (children[i]->is_leaf && children[i]->num_keys == 0 && tree->count > 0) { // internal nodes shouldn't point to empty leaf in non-empty tree
                    bptree_debug_print(tree->enable_debug, "Invariant Fail: Internal node %p points to empty leaf child[%d] in non-empty tree\n", (void*)node, i);
                    return false;
                }

                if (!bptree_check_invariants_node(children[i], tree, depth + 1, leaf_depth))  // depth + 1 because 
                    return false;
            }

            } else {
                if (!is_root || tree->count > 0) { // an internal node that has 0 keys in a non-empty tree
                    bptree_debug_print(tree->enable_debug, "Invariant Fail: Internal node %p has < 0 keys (%d)\n", (void*)node, node->num_keys);
                    return false;
                }
            }
            return true;
        }
    
}

// get the memory size needed to allocate a node
static size_t bptree_node_alloc_size(const bptree* tree, const bool is_leaf) {
    const int max_keys = tree->max_keys;
    const size_t keys_area_size = bptree_keys_area_size(max_keys);
    size_t data_payload_size;
    if (is_leaf) {
        data_payload_size = (size_t)(max_keys + 1) * sizeof(bptree_value_t); // if it's a leaf node it will hold values + one extra value for temporary overflow during the insertion
    } else {
        data_payload_size = (size_t)(max_keys + 2)* sizeof(bptree_node*); // if it's internal it will hold pointers, for n keys it will hold n+1 keys so max_keys + 1, and for temporary n + 1 (extra key) keys we need (n + 1) + 1
    }
    const size_t total_data_size_ = keys_area_size + data_payload_size; // keys + values or pointers
    return sizeof(bptree_node) + total_data_size_; // size of the header(the structure) + total
}

// this function allocate a bptree node iwth the alignement and alignement is ensuring that the data will start at a memory adress that is multiple of their size making it easier for cpu
static bptree_node* bptree_node_alloc(const bptree* tree, const bool is_leaf) {
    size_t max_align = alignof(bptree_node); // return the required alignement for a type
    max_align = (max_align > alignof(bptree_key_t)) ? max_align : alignof(bptree_key_t); // find the maximum align of bptree_node(the header) and bptree_key
    if (is_leaf) {
        max_align = (max_align > alignof(bptree_value_t)) ? max_align : alignof(bptree_value_t); // for a leaf that holds values find the largest one and return it
    } else {
        max_align = (max_align > alignof(bptree_node*)) ? max_align : alignof(bptree_node*); // for internal that holds pointer to node find the largest and return it
    }
    size_t size = bptree_node_alloc_size(tree, is_leaf); // the total size needed for a node
    
    /* IT WORKS ONLY ON POW2 BITES 2, 4, 8, 16 ...
        to a x be divisible by y x must have same 3 ending bits as y
        so we applied the mask or the clearing bits
        x = 76, y = 8; so 8 - 1 = ~(00000111) = 11111000 and 76 + 8 - 1 = (01010101)
        so we applied x mask y
        01010110  
        11111000
        01010000 = 80 > 76 divisible by 8
    */
    size = (size + max_align - 1) & ~(max_align - 1); // ~(max_align - 1) is the mask or the gate where (max_align - 1)'s bits will be reversed then applie the gate on size + max_align - 1
    bptree_node* node = aligned_alloc(max_align, size); // located in stdlib : allocate size starting from an adress that is multiple of max_align
    if (node) {
        node->is_leaf = is leaf;
        node->num_keys = 0;
        node->next = NULL;
    } else {
        bptree_debug_print(tree->enable_debug, "Node allocation failed (size: %zu, align: %zu)\n", size, max_align);
    }
    return node;
}

// recursively free a node and it's childrens
static void bptree_free_node(bptree_node* node, bptree* tree) {
    if (!node) return;
    if (!node->is_leaf) {
        bptree_node** children = bptree_node_children(node, tree->max_keys);
        for (int i = 0; i <= node->num_keys; i++) {
            bptree_free_node(children[i], tree);
        }
    }
    free(node);
}

// rebalancing the tree upward from a given node
/*
    after deletion, this function walks up the node stack and rebalances by borrowing keys from siblings or merging node if needed
*/

static void bptree_rebalance_up(bptree* tree, bptree_node** node_stack, // node_stack is array of node visited from root to where the deletion occured
    const int* index_stack, const int depth) { // index stack is an array of (the child number of parent) the path to the current node ,the child choosed in each parent from root to the current node
    for (int d = depth - 1; d >= 0; d--) { // loop bottom to up, starting from depth - 1 is the parent node from where the deletion occured
        bptree_node* parent = node_stack[d]; // the current node being examined
        const int child_idx = index_stack[d]; // which child of parent need to be fixed
        bptree_node** children = bptree_node_children(parent, tree->max_keys); // array of parent's childs pointers
        bptree_node* child = children[child_idx]; // the underflowing child (has few keys) that need to be fixed
        const int min_keys = child->is_leaf ? tree->min_leaf_keys : tree->min_internal_keys; // leaves and internal nodes can have differente mininmum key

        // if the node has enought keys, no need for rebalancing
        if (child->num_keys >= min_keys) {
            bptree_debug_print(tree->enable_debug, "Rebalancing unnecessary at depth %d, child %d has %d keys (min %d)\n", d, child_idx, child->num_keys, min_keys);
            break;
        }

        bptree_debug_print(tree->enable_debug, "Rebalancing needed at depth %d for child %d (%d keys < min %d)\n", d, child_idx, child->num_keys, min_keys);

        //Try borrowing frim the left sibling
        if (child_idx > 0) { // if not leftmostchild
            bptree_node* left_sibling = children[child_idx - 1]; // child_idx current child_idx - 1 left sibling 
            const int left_min = left_sibling->is_leaf ? tree->min_leaf_keys : tree->min_internal_keys;
            if (left_sibling->num_keys > left_min) {
                bptree_debug_print(tree->enable_debug, "Attempting borrow from left sibling (idx %d)\n", child_idx - 1);
                bptree_key_t* parent_keys = bptree_node_keys(parent); // get the parent keys to update separator later
                if (child->is_leaf) {
                    bptree_key_t* child_keys = bptree_node_keys(child); // retrieve keys
                    bptree_value_t* child_vals = bptree_node_values(child, tree->max_keys); // retrieve values array because it's leaf
                    const bptree_key_t* left_keys = bptree_node_keys(left_sibling); // retrieve left sibling's keys array 
                    const bptree_value_t* left_vals = bptree_node_values(left_sibling, tree->max_keys); // retrieve left sibling's values array 
                    
                    // shift keys and values right, to open space at index 0
                    // memove(des, src, n_bytes);
                    memmove(&child_keys[1], &child_keys[0], child->num_keys * sizeof(bptree_key_t)); // move the bytes in adress &child_keys[0] to &child_keys[1] by child->num_keys * sizeof(bptree_key_t) bytes
                    memmove(&child_vals[1], &child_vals[0], child->num_keys * sizeof(bptree_value_t)); // move the bytes in adress &child_vals[0] to &child_vals[1] by child->num_key * sizeof(bptree_value_t) bytes
                    
                    // move the last key/value from the left sibling.
                    child_keys[0] = left_keys[left_sibling->num_keys - 1];
                    child_vals[0] = left_keys[left_sibling->num_keys - 1];
                    child->num_keys++; // update keys count
                    left_sibling->num_keys--;

                    // update the parent separator
                    parent_keys[child_idx - 1] = child_keys[0];
                    bptree_debug_print(tree->enable_debug, "Borrowed leaf key from left. Parent key updated.\n");
                    break;
                } else {
                    // Internal node case: shift keys and children to insert the borrowed key.
                    bptree_key_t* child_keys = bptree_node_keys(child); // get the keys
                    bptree_node** child_children = bptree_node_children(child, tree->max_keys); // get the childrens because it's internal has nodes not vals
                    bptree_key_t* left_keys = bptree_node_keys(left_sibling); // get left sibling keys
                    bptree_node** left_children = bptree_node_children(left_sibling, tree->max_keys); //get children of left sibling

                    // move keys and childrens right to have room in 0
                    memmove(&child_keys[1], &child_keys[0], child->num_keys * sizeof(bptree_key_t)); // make room in keys[0]
                    memmove(&child_children[1], &child_children[0], (child->num_keys + 1) * sizeof(bptree_node*)); // move the pointers right

                    child_keys[0] = parent_keys[child_idx - 1]; // move the parent key separator to child 0
                    child_children[0] = left_children[left_sibling->num_keys]; // move the right most child pointer from left sibling

                    parent_keys[child_idx - 1] = left_keys[left_sibling->num_keys - 1]; // move the largest key of sibling to up parent

                    // update the count
                    child->num_keys++;
                    left_sibling->num_keys--;
                    bptree_debug_print(tree->enable_debug, "Borrowed internal key/child from left. Parent key updated.\n");
                    break;
                }
            }
        }

        // try borrowing from right siblings
        if (child_idx < parent->num_keys) { // if it's not the rightmost child
            bptree_node* right_sibling = children[child_idx + 1]; // get the right sibling
            const int right_min = right_sibling->is_leaf ? tree->min_leaf_keys : tree->min_internal_keys; // check if leaf or internal and get minimum key
            if (right_sibling->num_keys > right_min) { // if right sibling has more than minimum
                bptree_debug_print(tree->enable_debug, "Attempting borrow from right sibling (idx %d)\n", child_idx + 1);
                bptree_key_t* parent_keys = bptree_node_keys(parent); // get the key separator
                if (child->is_leaf) {
                    // Leaf node borrow form right sibling
                    bptree_key_t* child_keys = bptree_node_keys(child); // get the keys
                    bptree_value_t* child_vals = bptree_node_values(child, tree->max_keys); // get values
                    bptree_key_t* right_keys = bptree_node_keys(right_sibling); // get keys of right sibling
                    bptree_value_t* right_vals = bptree_node_values(right_sibling, tree->max_keys); // get the values of right sibling

                    // borrow the first key/value from the right sibling
                    child_keys[child->num_keys] = right_keys[0]; // move the leftmost key in the right sibling to the extra key
                    child_vals[child->num_keys] = right_vals[0]; // do the same with values
                    child->num_keys++; // update
                    right_sibling->num_keys++; // update

                    // shift right sibling's keys/values left.
                    memmove(&right_keys[0], &right_keys[1], right_sibling->num_keys * sizeof(bptree_key_t));
                    memmove(&right_vals[0], &right_vals[1], right_sibling->num_keys * sizeof(bptree_value_t));

                    // make the leftmost key in rightsibling as separator
                    parent_keys[child_idx] = right_keys[0];
                    bptree_debug_print(tree->enable_debug, "Borrowed leaf key from right. Parent key updated.\n");
                    
                    break;
                } else {
                    // Internal node: borrow key and child pointer from right sibling.
                    bptree_key_t* child_keys = bptree_node_keys(child); // get node keys
                    bptree_node** child_children = bptree_node_children(child, tree->max_keys); // get childrens
                    bptree_key_t* right_keys = bptree_node_keys(right_sibling); // get the rightsibling's keys
                    bptree_node** right_children = bptree_node_children(right_children, tree->max_keys); // get the rightsibling childrens
                    child_keys[child->num_keys] = parent_keys[child_idx]; // make the parent key as the extra node key
                    child_children[child->num_keys + 1] = right_children[0]; // make the extra child as the leftmost child of right sibling
                    parent_key[child_idx] = right_keys[0];
                    
                    // update the counts
                    child->num_key++;
                    right_sibling->num_keys--;

                    // make room in 0 key/childrens of right sibling
                    memmove(&right_keys[0], &right_keys[1], right_sibling->num_keys * sizeof(bptree_key_t));
                    memmove(&right_children[0], &right_children[1], right_sibling->num_keys * sizeof(bptree_node*))

                    bptree_debug_print(tree->enable_debug, "Borrowed internal key/child from right. Parent key updated.\n");
                    break;
                }
            }
        }
        
        // If borrowing failed attempt merge when the sibling nodes dont have enought key to borrow they merge into one node
        bptree_debug_print(tree->enable_debug, "Borrow failed attempting merge.\n");
        if (child_idx > 0) {
            // Merge with left siblings.
            bptree_node* left_sibling = children[child_idx - 1]; // get the left sibling
            bptree_debug_print(tree->enable_debug, "Merging child %d into left sibling %d\n", child_idx, child_idx - 1);
            if (child->is_leaf) {
                bptree_key_t* left_keys = bptree_node_keys(left_sibling); // key left sibling's keys
                bptree_value_t* left_vals = bptree_node_values(left_sibling, tree->max_keys); // get the leftsibling's values
                const bptree_key_t* child_keys = bptree_node_keys(child);
                const bptree_value_t* child_vals = bptree_node_values(child, tree->max_keys);
                const int combined_keys = left_sibling->num_keys + child->num_keys;
                if (combined_keys > tree->max_keys) {
                    fprintf(stderr, "[BPTree FATAL] Merge-Left (Leaf) Buffer Overflow PREVENTED! Combined keys %d > max_keys %d.\n", combined_keys, tree->max_keys);
                    abord();
                }

                // copy all keys and values from child to the left sibling
                memcpy(left_keys + left_sibling->num_keys, child_keys, child->num_keys * sizeof(bptree_key_t)); // copy child_keys to left_keys[left_sibling->num_keys]
                memcpy(left_vals + left_sibling->num_keys, child_vals, child->num_keys * sizeof(bptree_value_t)); // copy child_vals to left_vals[left_sibling->num_keys]

                left_sibling->num_keys = combined_keys;
                left_sibling->next = child->next; // child will be deleted it's next is the leftsibling's next
                
                free(child);
                children[child_idx] = NULL;
            } else { // if it's an internal node 
                bptree_key_t* left_keys = bptree_node_keys(left_sibling); // again get the leftsibling's key
                bptree_node** left_children = bptree_node_children(left_sibling, tree->max_keys); // get lft children
                bptree_key_t* child_keys = bptree_node_keys(child); // get node keys
                bptree_node** child_children = bptree_node_children(child, tree->max_keys); // get children
                const int combined_keys = left_sibling->num_keys + 1 + child->num_keys; // combined keys: 1 is for the parent key splitting them
                const int combined_children = (left_sibling->num_keys + 1) + (child->num_keys + 1);
                if (combined_keys > tree->max_keys) { // if combined keys are larger that max keys : overflow
                    fprintf(stderr, "[BPTree FATAL] Merge-Left (Internal) Key Buffer Overflow PREVENTED! Combined keys %d > buffer %d.\n",
                            combined_keys, tree->max_keys);
                            abord();
                }
                if (combined_children > tree->max_keys + 1) { // if combined childrens are larger that max childrens : overflow
                    fprintf(stderr,
                            "[BPTree FATAL] Merge-Left (Internal) Children Buffer Overflow "
                            "PREVENTED! Combined children %d > buffer %d.\n",
                            combined_children, tree->max_keys + 1);
                    abort();
                }

                // move the parent separator to the very right
                left_keys[left_sibling->num_keys] = parent_keys[child_idx - 1];
                
                // move the keys/childrens in right node to the left node after parent separator
                memcpy(left_keys + left_sibling->num_keys + 1, child_keys, child->num_keys * sizeof(bptree_key_t));
                memcpy(left_children + left_sibling->num_keys + 1, child_children, (child->num_keys + 1) * sizeof(bptree_node*));

                // update left node num keys and delete the child
                left_sibling->num_keys = combined_children;
                free(child);
                children[child_idx] = NULL;
            }

            // remove the parent separator that point to the merged node
            bptree_key_t* parent_keys = bptree_node_keys(parent);
            memmove(&parent_keys[child_idx - 1], &parent_keys[child_idx], (parent->num_keys - child_idx) * sizeof(bptree_key_t)); // move the key to the previous key which is the separator
            memmove(&children[child_idx], &children[child_idx + 1], (parent->num_keys - child_idx) * sizeof(bptree_node*)); // move the merged node into to deleted node
            parent->num_keys--; // keys dec
            bptree_debug_print(tree->enable_debug, "Merge with left complete. Parent updated.\n");
        } else {
            // Merge with right sibling if no left sibling is available
            bptree_node* right_sibling = children[child_idx + 1];
            bptree_debug_print(tree->enable_debug, "Mergin right sibling %d into child %d\n", child_idx + 1, child_idx);
            if (child->is_leaf) {
                bptree_key_t* child_key = bptree_node_keys(child);
                bptree_value_t* child_vals = bptree_node_values(child, tree->max_keys);
                const bptree_key_t* right_keys = bptree_node_keys(right_sibling);
                const bptree_value_t* right_vals = bptree_node_values(right_sibling, tree->max_keys);
                const int combined_keys = child->num_keys + right_children->num_keys;
                if (combined_keys > tree->max_keys) {
                    fprintf(stderr, "[BPTree FATAL] Merge-Right (Leaf) Buffer Overflow PREVENTED! Combined keys %d > max_keys %d.\n", combined_keys, tree->max_keys);
                    abord();
                }

                memcpy(child_keys + child->num_keys, right_keys, right_sibling->num_keys * sizeof(bptree_key_t));
                memcpy(child_vals + child->num_keys, right_vals, right_sibling->num_keys * sizeof(bptree_value_t));


                child->num_keys = combined_keys;
                child->next = right_sibling->next;

                free(right_children);
                children[child_idx + 1] = NULL;
            } else {
                bptree_key_t* child_keys = bptree_node_keys(child);
                bptree_node** child_children = bptree_node_children(child, tree->max_keys);
                const bptree_key_t* right_keys = bptree_node_keys(right_sibling);
                const bptree_node** right_children = bptree_node_children(right_children, tree->max_keys);
                const bptree_key_t* parent_keys = bptree_node_keys(parent);
                const int combined_keys = child->num_keys + 1 + right_sibling->num_keys;
                const int combined_children = child->num_keys + 1 + right_sibling->num_keys + 1;
                if (combined_keys > tree->max_keys) {
                    fprintf(stderr,
                            "[BPTree FATAL] Merge-Right (Internal) Key Buffer Overflow PREVENTED! "
                            "Combined keys %d > buffer %d.\n",
                            combined_keys, tree->max_keys);
                    abort();
                }
                if (combined_children > tree->max_keys + 1) {
                    fprintf(stderr,
                            "[BPTree FATAL] Merge-Right (Internal) Children Buffer Overflow "
                            "PREVENTED! Combined children %d > buffer %d.\n",
                            combined_children, tree->max_keys + 1);
                    abort();
                }
                child_keys[child->num_keys] = parent_keys[child_idx];

                memcpy(child_keys + child->num_keys + 1, right_keys, (right_sibling->num_keys + 1) * sizeof(bptree_key_t));
                memcpy(child_children + child->num_keys + 1, right_children, (right_sibling->num_keys + 1) * sizeof(bptree_node*));
                
                child->num_keys = combined_keys;
                free(right_sibling);
                children[child_idx] = NULL;
            }
            bptree_key_t* parent_keys = bptree_node_keys(parent);
            memmove(&parent_keys[child_idx], &parent_keys[child_idx + 1],
                    (parent->num_keys - child_idx - 1) * sizeof(bptree_key_t));
            memmove(&children[child_idx + 1], &children[child_idx + 2],
                    (parent->num_keys - child_idx - 1) * sizeof(bptree_node*));
            parent->num_keys--;
            bptree_debug_print(tree->enable_debug, "Merge with right complete. Parent updated.\n");

        }
    }
    
    // check a special case when root can became empty and depth is reduced
    if (tree->root && !tree->root->is_leaf && tree->root->num_keys == 0)



}


}
    






#endif