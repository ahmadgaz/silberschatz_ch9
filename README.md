# Contiguous Memory Allocator

## build

```bash
gcc -std=c99 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined allocator.c -o allocator
```

## run
Start the allocator with a given total memory size in bytes, for example:

```bash
./allocator 1048576
```

The program accepts the following commands:
- `RQ P<pid> <size> {F|B|W}` - Request `<size>` bytes for process `<pid>` using:
    - `F` = First fit
    - `B` = Best fit
    - `W` = Worst fit
- `RL P<pid>` - Release the region owned by process `<pid>`
- `C` - Compact memory (pacl all processes to the beginning and merge holes into one)
- `STAT` - Print current memory layout
- `X` - exit the program

Example session:

```
allocator> RQ P1 40000 F
allocator> STAT
Addresses [0:39999] Process P1
Addresses [40000:1048575] Unused
allocator> X
```

## automated tests

You can run a predefined set of tests by putting line-seperated commands into a file, ending with the `X` command (e.g. `text.txt`):

```
STAT
RQ P1 200 F
RQ P2 300 F
RQ P3 100 F
STAT
RL P2
STAT
C
STAT
RQ P4 100 B
RQ P5 50 W
STAT
RQ P6 2000 F
RQ P7 10 Z
RL P999
X
```

Then run:

```bash
./allocator 1000 < test.txt > output.txt 2>&1
```

`output.txt` will contain prompts and status outputs along with any error messages.
