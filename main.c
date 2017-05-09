#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>

/*

Key things to avoid:
    - mixing up sizes of flat types
    - mixing up references and flat types

types are:
    - int
    - float
    - struct { type ... }

new types are constructed using the mktuple instruction:
    - takes a static integer N
    - pops N types from the type stack only: pushes a new struct-type.
    - the operand stack stays in place.

To make this efficient, maybe types should have a string representation:
    int = "i"
    float = "f"
    struct { int, int, float } = "s3iif"

so to build this data:
    struct { 3, 4, 5.0 }
you would execute this:
    push_float 5.0
    push_int 4
    push_int 3
    construct 3
and the two stacks would evolve like this:
    type:        data:
           >.                       >.
          >f.                >[ 5.0 ].
         >if.           >[ 4 ][ 5.0 ].
        >iif.      >[ 3 ][ 4 ][ 5.0 ].
      >s3iif.      >[ 3 ][ 4 ][ 5.0 ].
and then it's easy to unpack a struct:
    destruct 3
only touches the type stack: it asserts and pops "s3".

now instead of strings you actually want to use a small struct:
*/
typedef struct Ty {
    // low-order bitfields come first
    enum { kind_UNINITIALIZED = 0, kind_I32, kind_F64, kind_STRUCT } kind:2;
    unsigned int struct_len:6;
    // with 6 bits for struct_len, you can have up to 63 fields.
    // TODO maybe use the length in bytes instead!
    // - encodes the struct length implicitly
    // - makes it easier to copy
    // - easier to check for underflow on cons
} Ty;
// TODO import the actual known-size types from that header...
Ty I32 = { kind_I32, 0 };
Ty F64 = { kind_F64, 0 };
Ty STRUCT(int size) { Ty v = { kind_STRUCT, size }; return v; }

int eq_ty(Ty a, Ty b) { return !memcmp(&a, &b, sizeof(Ty)); }
void assert_eq_ty(Ty a, Ty b) {
    if (!eq_ty(a, b)) {
        printf("a = { %d, %d }; b = { %d, %d }\n", a.kind, a.struct_len, b.kind, b.struct_len);
        assert(0 && "type tag mismatch");
    }
}

typedef int    i32;
typedef double f64;

////////////////////////////////////////////////
// stacks and unsafe operations
////////////////////////////////////////////////

// these two buffers don't necessarily need the same size:
//  all-scalar data needs 1 type byte per data word.
//  but deeply nested structs need more type bytes.
int data_buffer[1024 * 1024];
Ty  type_buffer[1024 * 1024];
void *data_ptr = (void*)&data_buffer + sizeof(data_buffer);
Ty   *type_ptr = &type_buffer[sizeof(type_buffer)/sizeof(type_buffer[0]) - 1];
// type_ptr starts with one dummy/zero/uninitialized element to prevent underflow

void unsafe_push_data_i32(i32 v) { data_ptr -= sizeof(i32); *(i32*)data_ptr = v; }
void unsafe_push_data_f64(f64 v) { data_ptr -= sizeof(f64); *(f64*)data_ptr = v; }
i32 unsafe_pop_data_i32() { i32 v = *(i32*)data_ptr; data_ptr += sizeof(i32); return v; }
f64 unsafe_pop_data_f64() { f64 v = *(f64*)data_ptr; data_ptr += sizeof(f64); return v; }

void unsafe_push_type(Ty ty) { *--type_ptr = ty; }
Ty   unsafe_pop_type ()      { return *type_ptr++; }

// TODO add struct operations
// - watch out for stack underflow when consing

void push_i32(i32 v) { unsafe_push_type(I32); unsafe_push_data_i32(v); }
void push_f64(f64 v) { unsafe_push_type(F64); unsafe_push_data_f64(v); }
i32 pop_i32() { Ty t = unsafe_pop_type(); assert_eq_ty(t, I32); return unsafe_pop_data_i32(); }
f64 pop_f64() { Ty t = unsafe_pop_type(); assert_eq_ty(t, F64); return unsafe_pop_data_f64(); }


int main() {
    push_i32(3);
    push_i32(4);
    push_f64(5);

    f64 f = pop_f64(); // 5.0
    i32 x = pop_i32(); // 4
    i32 y = pop_i32(); // 3
    printf("%d %d %f\n", y, x, f); // 3 4 5.0
}
