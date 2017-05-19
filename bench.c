#include "vm.c"
int main() {
push_i32(2);
push_i32(3);
add_i32();
grab(0);
grab(0);
mul_i32();
}
