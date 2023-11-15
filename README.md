# Equivalence Modulo Inputs

## Introduction

The original paper of EMI lacks an implementation, this repo provides a straigtforward yet simple implementation based on libclang.

Illustration about this implementation:
```
i.c -> EMIInstrum -> t.c // instrument all blocks
run t.c // obtain covered blocks, compute and print dead blocks
i.c -> EMIMutator -> o.c // prune a dead block (specified from cmdline)
```


## Installation

```
sudo apt install libclang-12-dev libllvm12 llvm-12-dev llvm-12-tools
```

## Compile and run

Steps to compile this project:
```
mkdir output
cd output
cmake ..
make
```

Steps to run:
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
