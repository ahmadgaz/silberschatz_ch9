# page-replacement simulator (fifo, lru, opt)

this program simulates three page-replacement algorithms:

- fifo (first-in, first-out)
- lru (least recently used)
- opt (optimal / clairvoyant)

page references are generated randomly in the range 0..9.

## build

```bash
gcc -Wall -Wextra -std=c11 pager.c -o pager
````

## usage

```bash
./pager <num_frames> [ref_len] [seed]
```

* `num_frames` (required): number of page frames (1..50)
* `ref_len`   (optional): length of the page-reference string (default `20`, max `1000`)
* `seed`      (optional): random seed for reproducible tests (default is current time)

example:

```bash
./pager 3 20 1
```

## output

for each run the program prints:

* the number of frames, reference length, and random seed

* the generated page-reference string

* a table with one row per reference:

  * `Ref` column: current page reference
  * `FIFO`, `LRU`, `OPT` columns:

    * show frame contents as digits (or `.` for empty)
    * append `F` to mark a page fault, or `.` for a hit

* total page faults for each algorithm:

```text
faults FIFO = <number>
faults LRU  = <number>
faults OPT  = <number>
```

## test cases

example tests (output redirected to files):

```bash
./pager 3 10 1 > test_3frames_10refs.txt
./pager 3 20 2 > test_3frames_20refs.txt
./pager 5 20 3 > test_5frames_20refs.txt
./pager 7 50 4 > test_7frames_50refs.txt
```

each file contains the random reference string and the page-fault counts
for fifo, lru, and opt for that particular test.
