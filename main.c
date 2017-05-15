#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#define inline

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

typedef int32_t  i32;
typedef double_t f64;

////////////////////////////////////////////////
// stacks and unsafe operations
////////////////////////////////////////////////

// these two buffers don't necessarily need the same size:
//  all-scalar data needs 1 type byte per data word.
//  but deeply nested structs need more type bytes.
static char data_buffer[4 * 1024 * 1024];
static Ty   type_buffer[1024 * 1024];
static char *data_ptr = &data_buffer[0];
static Ty   *type_ptr = &type_buffer[1];
// type_ptr starts with one dummy/zero/uninitialized element to prevent underflow

inline void unsafe_push_data_i32(i32 v) { *(i32*)data_ptr = v; data_ptr += sizeof(i32); }
inline void unsafe_push_data_f64(f64 v) { *(f64*)data_ptr = v; data_ptr += sizeof(f64); }
inline i32 unsafe_pop_data_i32() { data_ptr -= sizeof(i32); i32 v = *(i32*)data_ptr; return v; }
inline f64 unsafe_pop_data_f64() { data_ptr -= sizeof(f64); f64 v = *(f64*)data_ptr; return v; }

inline void unsafe_push_type(Ty ty) { *type_ptr++ = ty; }
inline Ty   unsafe_pop_type ()      { return *--type_ptr; }

void print_stack() {
    // iterate over the type stack nodes from left to right: oldest-pushed first.
    char *dp = &data_buffer[0];
    for (Ty *tp = &type_buffer[0]; tp < type_ptr; ++tp) {
        assert(dp <= data_ptr);
        switch (tp->kind) {
            case kind_UNINITIALIZED:
                printf("[ undef ]");
                break;
            case kind_I32:
                printf("[ i32 %d ]", *(i32*)dp);
                dp += sizeof(i32);
                break;
            case kind_F64:
                printf("[ f64 %f ]", *(f64*)dp);
                dp += sizeof(f64);
                break;
            case kind_STRUCT:
                printf(" s%d ", tp->struct_len);
                break;
            default:
                assert(0 && "bad kind on type stack");
                break;
        }
    }
    printf("\n");
}
void debug_print_stack() {
    //print_stack();
}

// TODO add C FFI
// - type-tag for a function needs to include argument and result types

// TODO add custom type-tag operations
// - make a fresh type tag
// - construct/destruct of a newtype only touches the type stack

// TODO can you define safe heap ops in userland in terms of newtype+malloc+refcounting?


////////////////////////////////////////////////
// safe operations
////////////////////////////////////////////////

// num_values is a higher-level concept than the data or type stack:
// - data stack contains raw bytes
// - type stack contains type constructor nodes; a single struct value takes up several of these.
// num_values simply means how many pushes ever done, minus how many pops ever done.
static int   num_values = 0;
inline void push_i32(i32 v) { unsafe_push_type(I32); unsafe_push_data_i32(v); ++num_values; debug_print_stack(); }
inline void push_f64(f64 v) { unsafe_push_type(F64); unsafe_push_data_f64(v); ++num_values; debug_print_stack(); }
inline i32 pop_i32() { Ty t = unsafe_pop_type(); assert_eq_ty(t, I32); --num_values; i32 v = unsafe_pop_data_i32(); debug_print_stack(); return v; }
inline f64 pop_f64() { Ty t = unsafe_pop_type(); assert_eq_ty(t, F64); --num_values; f64 v = unsafe_pop_data_f64(); debug_print_stack(); return v; }
inline void construct(int num_fields) {
    // check for underflow
    assert(num_values >= num_fields);
    unsafe_push_type(STRUCT(num_fields));
    num_values -= num_fields; // remove the fields
    num_values++; // add the struct
    debug_print_stack();
}
inline void destruct(int num_fields) {
    Ty t = unsafe_pop_type();
    assert_eq_ty(t, STRUCT(num_fields));
    num_values--; // remove the struct
    num_values += num_fields; // add the fields back on
    debug_print_stack();
}
typedef struct stackindex { Ty *tp; char *dp; } stackindex;
stackindex scan_back(int num_to_skip, Ty *ty_start, char *data_start) {
    // TODO optimize this to not need a loop:
    // maybe keep a table that lets you index back with one indirection.
    // but: keeping that table up to date could be hard for the cut operation.
    //   - you could memmove the table just like the other stacks, except the ptr targets also moved
    //   - maybe the table could store some kind of offset?
    stackindex result;
    result.tp = ty_start;
    result.dp = data_start;
    int remaining_elements = num_to_skip;
    while (remaining_elements > 0) {
        --result.tp;
        switch (result.tp->kind) {
            case kind_UNINITIALIZED:
                assert(0 && "underflow in scan_back");
                break;
            case kind_I32:
                result.dp -= sizeof(i32);
                --remaining_elements;
                break;
            case kind_F64:
                result.dp -= sizeof(f64);
                --remaining_elements;
                break;
            case kind_STRUCT:
                // no need to update data pointer in this case
                // instead, if the struct has 4 fields, we decrement remaining_elements for the struct,
                --remaining_elements;
                // but now there are 4 additional items to scan back over,
                // so increment remaining_elements by struct_len.
                remaining_elements += result.tp->struct_len;
                break;
            default:
                assert(0 && "bad kind in scan_back");
                break;
        }
    }
    return result;
}
inline void grab(int index) {
    assert(index < num_values && "underflow in grab");
    // find the indexth element of the stack and push a copy of it

    // skip backwards over `index` items
    stackindex right_index = scan_back(index, type_ptr, data_ptr);
    // skip to start of item we want to grab
    stackindex left_index = scan_back(1, right_index.tp, right_index.dp);
    // update type stack
    {
        int nnodes = right_index.tp - left_index.tp;
        memcpy(type_ptr, left_index.tp, nnodes*sizeof(Ty));
        type_ptr += nnodes;
    }
    // update value stack
    {
        int nbytes = right_index.dp - left_index.dp;
        memcpy(data_ptr, left_index.dp, nbytes);
        data_ptr += nbytes;
    }
    // update num_values
    ++num_values;

    debug_print_stack();
}
inline void cut(int start, int num_to_remove) {
    // skip over `start` elements, and remove `num_to_remove`

    // skip backwards over `start` items
    stackindex right_index = scan_back(start, type_ptr, data_ptr);
    // skip backwards over `num_to_remove` additional items
    stackindex left_index = scan_back(num_to_remove, right_index.tp, right_index.dp);
    // update type stack
    {
        int nnodes_to_move = type_ptr - right_index.tp;
        memmove(left_index.tp, right_index.tp, nnodes_to_move*sizeof(Ty));
        int nnodes_to_cut = right_index.tp - left_index.tp;
        type_ptr -= nnodes_to_cut;
    }
    // update data stack
    {
        int nbytes_to_move = data_ptr - right_index.dp;
        memmove(left_index.dp, right_index.dp, nbytes_to_move);
        int nbytes_to_cut = right_index.dp - left_index.dp;
        data_ptr -= nbytes_to_cut;
    }
    // update num_values
    num_values -= num_to_remove;
 
    debug_print_stack();
}


////////////////////////////////////////////////
// more ops - defined in terms of safe push/pop
////////////////////////////////////////////////

#define DEF_BINOP_FOR_TYPE(arg, ret, name, op) \
    inline void name##_##arg() { arg right=pop_##arg(); arg left=pop_##arg(); push_##ret(left op right); }
#define DEF_BINOP_ARITH(name, op) \
    DEF_BINOP_FOR_TYPE(i32, i32, name, op) \
    DEF_BINOP_FOR_TYPE(f64, f64, name, op)
#define DEF_BINOP_REL(name, op) \
    DEF_BINOP_FOR_TYPE(i32, i32, name, op) \
    DEF_BINOP_FOR_TYPE(f64, i32, name, op)

// arithmetic
DEF_BINOP_ARITH(add, +)
DEF_BINOP_ARITH(sub, -)
DEF_BINOP_ARITH(mul, *)
DEF_BINOP_ARITH(div, /)
DEF_BINOP_FOR_TYPE(i32, i32, mod, %)
// comparisons
DEF_BINOP_REL(lt,  <)
DEF_BINOP_REL(lte, <=)
DEF_BINOP_REL(gt,  >)
DEF_BINOP_REL(gte, >=)
DEF_BINOP_REL(eq,  ==)
DEF_BINOP_REL(neq, !=)




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

inline void sqrt_f64() {
    push_f64(sqrt(pop_f64()));
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

void add_vec2() {
    // a{x, y} b{x, y}
    // TODO need a bunch of stack operations to make this work!
    // - grab an arbitrary value at index i and push it
    // - cut out a region of the stack, to return the top few elements

    /*
    plan:
        ax, ay = destruct a
        bx, by = destruct b
        construct(ax + bx, ay + by)

        // Without compiler support, we need to copy the argument to destruct.
        // - can you do linear-type inference on the expr lang?
        // - can peephole opt on the bytecode avoid the copy??
        //   - you want to float the destruct earlier in the stream, then merge it with the dup??
        grab a
        destruct 2
        grab b
        destruct 2
        // ax ay bx by
        grab ax
        grab bx
        add
        grab bx
        grab by
        add
        construct 2
        // a{x, y} b{x, y} ax ay bx by c{x, y}
        cut 6 stack values, skipping 1
        // c{x, y}
        // now we can return.

        so the new instructions are:
            - dup(grab) i
            - cut start end

    * /
    // a{ax, ay} b{bx, by}
    grab(1);
    destruct(2);
    // a{ax, ay} b{bx, by} ax ay
    grab(2);
    destruct(2);
    // a{ax, ay} b{bx, by} ax ay bx by
    grab(3);
    // a{ax, ay} b{bx, by} ax ay bx by ax
    grab(2);
    // a{ax, ay} b{bx, by} ax ay bx by ax bx
    add_f64();
    // a{ax, ay} b{bx, by} ax ay bx by cx
    grab(3);
    // a{ax, ay} b{bx, by} ax ay bx by cx ay
    grab(2);
    // a{ax, ay} b{bx, by} ax ay bx by cx ay by
    add_f64();
    // a{ax, ay} b{bx, by} ax ay bx by cx cy
    construct(2);
    // a{ax, ay} b{bx, by} ax ay bx by c{cx, cy}
    cut(1, 6); // skip 1 and cut 6 values
    // c{cx, cy}
    // */
}

void bench_fib() {
    // compare to
    // time python -c 'fib = lambda n: n if n < 2 else fib(n-1) + fib(n-2); print fib(32)'
    for (i32 i=0; i<33; ++i) {
        printf("%d %d\n", i, fib_C(i));
    }
}

void bench_sum() {
    f64 max = 1.0e8;
    push_f64(0.0); // sum
    push_f64(0.0); // sum i
    while ((grab(0), push_f64(max), lt_f64(), pop_i32())) {
        // sum i
        grab(1); grab(1); add_f64(); // sum i sum'
        grab(1); push_f64(1.0); add_f64(); // sum i sum' i'
        cut(2, 2);
    }
    pop_f64();
    f64 sum = pop_f64();
    printf("bench_sum %f\n", sum);
}

void bench_sum_native() {
    f64 sum = 0.0;
    for (f64 i = 0.0; i < 1000000.0; ++i) {
        sum += i;
    }
    printf("bench_sum %f\n", sum);
}

int main() {
    assert(sizeof(i32) == 4);
    assert(sizeof(f64) == 8);

    //bench_sum_native();
    bench_sum();
}
