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
$ ./EMI ../tests/a.c -o o.c # this will print dead blocks
dead blocks: 1 2 3
$ ./EMI ../tests/a.c -o o.c --remove=1
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
