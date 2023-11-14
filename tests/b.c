int f() {
  if (1) {
    printf("block xx\n");
  }
  for (int i = 0; i < 10; i++) {
    printf("block yy\n");
  }
  printf("block dd\n");
  return 0;
}

int main() {
  printf("block zz\n");
  return f();
}
