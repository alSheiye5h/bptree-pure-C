#ifndef BPTREE_H
#define BPTREE_H 

#ifdef __cplusplus
extern "C" {
#endif

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








}
#endif