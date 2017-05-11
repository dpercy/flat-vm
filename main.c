#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>

/*

TODO how do local variables work? unlike values on a stack, local variables are mutable.
and changing a value means it could change size.
    local x = {1, 2}
    local y = {3, 4}
    x = {5, 6, 7, 8, 9}  # how do you grow the storage here?
some options:
- use heap boxes and realloc for locals
  - defeats the purpose of this experiment!!!
- require locals to be immutable; use local continuations for loops
  - are you going to pass *all* the locals though?
  - needs more compiler work
  - good for closures anyway
- require locals to not change size!
  - when initializing a local, just push
  - when mutating a local, raise an error if the size changes
so assuming we prohibit mutating locals,
maybe we can compile a subset of scheme to this VM.
no loops: just let-labels like in "Compiling without Continuations"





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
static Ty I32 = { kind_I32, 0 };
static Ty F64 = { kind_F64, 0 };
Ty STRUCT(int size) { Ty v = { kind_STRUCT, size }; return v; }

inline int eq_ty(Ty a, Ty b) { return !memcmp(&a, &b, sizeof(Ty)); }
inline void assert_eq_ty(Ty a, Ty b) {
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
static int data_buffer[1024 * 1024];
static Ty  type_buffer[1024 * 1024];
void *data_ptr = (void*)&data_buffer + sizeof(data_buffer);
Ty   *type_ptr = &type_buffer[sizeof(type_buffer)/sizeof(type_buffer[0]) - 1];
// type_ptr starts with one dummy/zero/uninitialized element to prevent underflow

inline void unsafe_push_data_i32(i32 v) { data_ptr -= sizeof(i32); *(i32*)data_ptr = v; }
inline void unsafe_push_data_f64(f64 v) { data_ptr -= sizeof(f64); *(f64*)data_ptr = v; }
inline i32 unsafe_pop_data_i32() { i32 v = *(i32*)data_ptr; data_ptr += sizeof(i32); return v; }
inline f64 unsafe_pop_data_f64() { f64 v = *(f64*)data_ptr; data_ptr += sizeof(f64); return v; }

inline void unsafe_push_type(Ty ty) { *--type_ptr = ty; }
inline Ty   unsafe_pop_type ()      { return *type_ptr++; }

// TODO add struct operations
// - watch out for stack underflow when consing

// TODO add C FFI
// - type-tag for a function needs to include argument and result types

// TODO add custom type-tag operations
// - make a fresh type tag
// - construct/destruct of a newtype only touches the type stack

// TODO can you define safe heap ops in userland in terms of newtype+malloc+refcounting?


////////////////////////////////////////////////
// safe operations
////////////////////////////////////////////////

inline void push_i32(i32 v) { unsafe_push_type(I32); unsafe_push_data_i32(v); }
inline void push_f64(f64 v) { unsafe_push_type(F64); unsafe_push_data_f64(v); }
inline i32 pop_i32() { Ty t = unsafe_pop_type(); assert_eq_ty(t, I32); return unsafe_pop_data_i32(); }
inline f64 pop_f64() { Ty t = unsafe_pop_type(); assert_eq_ty(t, F64); return unsafe_pop_data_f64(); }

// TODO need more stack operations for local variables...


////////////////////////////////////////////////
// more ops - defined in terms of safe push/pop
////////////////////////////////////////////////

#define DEF_BINOP_FOR_TYPE(t, name, op) \
    inline void name##_##t() { t right=pop_##t(); t left=pop_##t(); push_##t(left op right); }
#define DEF_BINOP(name, op) \
    DEF_BINOP_FOR_TYPE(i32, name, op) \
    DEF_BINOP_FOR_TYPE(f64, name, op)

// arithmetic
DEF_BINOP(add, +)
DEF_BINOP(sub, -)
DEF_BINOP(mul, *)
DEF_BINOP(div, /)
DEF_BINOP_FOR_TYPE(i32, mod, %)
// TODO relational operators must always return a boolean
DEF_BINOP_FOR_TYPE(i32, lt, <)




////////////////////////////////////////////////
// entry point
////////////////////////////////////////////////

inline void dup_i32() {
    i32 x = pop_i32();
    push_i32(x);
    push_i32(x);
}

inline void swap_i32() {
    i32 x = pop_i32();
    i32 y = pop_i32();
    push_i32(x);
    push_i32(y);
}

// [ i32 ] -> [ i32 ]
void fib() {
    // n
    dup_i32(); // n n
    push_i32(2); // n n 2
    lt_i32(); // n (n < 2)
    if (pop_i32()) {
        // n
    } else {
        // n
        dup_i32(); // n n
        push_i32(1); // n n 1
        sub_i32(); // n (n - 1)
        fib(); // n fib(n - 1)
        swap_i32(); // fib(n - 1) n
        push_i32(2); // fib(n - 1) n 2
        sub_i32(); // fib(n - 1) (n - 2)
        fib(); // fib(n - 1) fib(n - 2)
        add_i32();
    }
}

i32 fib_C(i32 n) {
    push_i32(n);
    fib();
    return pop_i32();
}

int main() {
    for (i32 i=0; i<33; ++i) {
        printf("%d %d\n", i, fib_C(i));
    }
}
