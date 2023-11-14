# A straightforward implementation of EMI.

## compile and run

```
mkdir output
cd output
cmake ..
make
```

```
$ cd output
$ ./EMI ../tests/b.c -o o.c
dead blocks: 1 2 3 # this will print dead blocks
$ ./EMI ../tests/b.c -o o.c --remove=1
dead blocks: 1 2 3
try to remove 1
$ cat o.c
int f() {
  if (0) {} // removed
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
```
