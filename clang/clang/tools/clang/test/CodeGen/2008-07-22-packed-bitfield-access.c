// RUN: clang %s -emit-llvm -o -

int main () {
  struct foo {
    unsigned a:16;
    unsigned b:32 __attribute__ ((packed));
  } x;
  x.b = 0x56789abcL;
  return 0;
}
