int f() {
  if (0) {
    printf("block XXX\n");
    if (0) {
      printf("block XXX\n");
    }
  }
  if (0) {
    printf("block XXX\n");
  }
  for (int i = 0; i < 10; i++) {
    printf("block YYY\n");
  }
  printf("block ZZZ\n");
  return 0;
}

int main() {
  printf("block DDD\n");
  return f();
}
