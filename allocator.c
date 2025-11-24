#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#define MAX_LINE 256
#define HASHSIZE 10

/* --- Region --- */ 
typedef enum {
    E_MEM = 1,
    E_FLAG,
    E_NULLREG,
    E_NOTHOLE,
    E_NOTDEF,
    E_HOLETOOSMALL,
    E_EMPTY,
    E_KIND,
    E_PID,
    E_ADJ
} error;
typedef enum { R_HOLE, R_PROC } region_kind;
typedef struct { unsigned long start, size; } span;
typedef struct region region;
struct region
{
    region *prev;
    region *next;
    region_kind kind;
    span pos;
    unsigned long pid;
};

/* --- Process Table --- */
typedef struct proc proc;
struct proc
{
    proc *prev;
    proc *next;
    region *reg;
};
static proc *proc_table[HASHSIZE];

/* --- Routines --- */
static void region_init (
    region *reg,
    region_kind kind,
    unsigned long start,
    unsigned long size,
    unsigned long pid)
{
    reg->prev = NULL;
    reg->next = NULL;
    reg->kind = kind;
    reg->pos.start = start;
    reg->pos.size = size;
    reg->pid = pid;
}
static void mem_init (region *mem, unsigned long size)
{
    region_init(
        mem,
        R_HOLE,
        0,
        size,
        0);
}
static unsigned hash (unsigned long pid) { return pid % HASHSIZE; }
static proc *lookup (unsigned long pid)
{
    // walk bucket chain for pid
    for (proc *p = proc_table[hash(pid)]; NULL != p; p = p->next)
        if (NULL != p->reg && p->reg->pid == pid) return p;
    return NULL;
}
static int install (unsigned long pid, region *reg)
{
    // 1. see if proc already exists
    proc *p = lookup(pid);
    unsigned hashval;
    if (NULL == p)
    {
        // 2. allocate new proc entry
        p = (proc *)malloc(sizeof(proc));
        if (NULL == p) return -1;
        p->reg = reg;
        hashval = hash(pid);

        // 3. insert at head of bucket list
        if (NULL != proc_table[hashval]) proc_table[hashval]->prev = p;
        p->prev = NULL;
        p->next = proc_table[hashval];
        proc_table[hashval] = p;
    }

    // 4. update existing entry
    else p->reg = reg;

    return 0;
}
static int undef (unsigned long pid)
{
    // 1. find proc entry
    proc *p = lookup(pid);
    if (NULL == p) return E_NOTDEF;

    unsigned hashval = hash(pid);

    // 2. unlink from bucket list
    if (NULL != p->prev) p->prev->next = p->next;
    else proc_table[hashval] = p->next;
    if (NULL != p->next) p->next->prev = p->prev;

    // 3. free entry
    free(p);

    return 0;
}
static region *find_first_fit_hole (region *mem, unsigned long size)
{
    // 1. scan list for first big-enough hole
    for (region *reg = mem; NULL != reg; reg = reg->next)
    {
        if (R_HOLE != reg->kind) continue;
        if (reg->pos.size >= size) return reg;
    }

    // 2. no suitable hole found ;(
    return NULL;
}
static region *find_best_fit_hole (region *mem, unsigned long size)
{
    // 1. track the smallest hole that fits
    region *hole = NULL;
    for (region *reg = mem; NULL != reg; reg = reg->next)
    {
        if (R_HOLE != reg->kind) continue;
        if (
            reg->pos.size >= size
            && (
                NULL == hole
                || reg->pos.size < hole->pos.size))
            hole = reg;
    }

    // 2. return best fit hole or NULL
    return hole;
}
static region *find_worst_fit_hole (region *mem, unsigned long size)
{
    // 1. track largest hole that fits
    region *hole = NULL;
    for (region *reg = mem; NULL != reg; reg = reg->next)
    {
        if (R_HOLE != reg->kind) continue;
        if (
            reg->pos.size >= size
            && (
                NULL == hole
                || reg->pos.size > hole->pos.size))
            hole = reg;
    }

    // 2. return worst fit hole or NULL
    return hole;
}
static int combine (region *a, region *b)
{
    // 1. ensure same kind and pid
    if (a->kind != b->kind) return E_KIND;
    if (a->pid != b->pid) return E_PID;

    // 2. normalize order so a precedes b
    if (b->next == a)
    {
        region *tmp = b;
        b = a;
        a = tmp;
    }
    else if (a->next != b) return E_ADJ;

    // 3. grow a to include b
    a->pos.size += b->pos.size;

    // 4. relink list and free b
    if (NULL != b->next) b->next->prev = a;
    a->next = b->next;
    free(b);

    return 0;
}

static int allocate (
    region *new,
    region *hole,
    unsigned long pid,
    unsigned long size)
{
    // 1. validate arguments and hole
    if (NULL == hole || NULL == new) return E_NULLREG;
    if (0 == size) return E_EMPTY;
    if (R_HOLE != hole->kind) return E_NOTHOLE;
    if (hole->pos.size < size) return E_HOLETOOSMALL;

    // 2. init new process region at hole start
    region_init(
        new,
        R_PROC,
        hole->pos.start,
        size,
        pid);

    // 3. shrink the holes span
    hole->pos.start += size;
    hole->pos.size -= size;

    // 4. splice new before hole
    new->next = hole;
    new->prev = hole->prev;
    if (NULL != hole->prev) hole->prev->next = new;
    hole->prev = new;

    // 5. if hole is empty, remove it and update connections
    if (0 == hole->pos.size)
    {
        if (NULL != hole->next)
        {
            hole->next->prev = new;
            new->next = hole->next;
        }
        else new->next = NULL;
        free(hole);
    }
    
    return 0;
}

int request_reg (
    region **mem,
    unsigned long pid,
    unsigned long size, 
    char flag)
{
    // 1. basic checks
    if (NULL == *mem) return E_NULLREG;
    if (0 == size) return E_EMPTY;

    region *hole = NULL;
    region *new = NULL;

    // 2. try to extend existing region if possible
    proc *existing = lookup(pid);
    if (NULL != existing && NULL != existing->reg)
    {
        if (NULL != existing->reg->next
            && R_HOLE == existing->reg->next->kind
            && existing->reg->next->pos.size >= size)
            hole = existing->reg->next;
        else return E_MEM;
    }

    // 3. allocation by extending existing region
    if (NULL != hole)
    {
        new = (region *)malloc(sizeof(region));
        if (NULL == new) return -1;
        int ret = allocate(new, hole, pid, size);
        if (0 != ret)
        {
            free(new);
            return ret;
        }
        return combine(existing->reg, new);
    }

    // 4. if no existing region, select a hole by strategy
    switch (flag)
    {
        case 'F':
            hole = find_first_fit_hole(*mem, size);
            break;
        case 'B':
            hole = find_best_fit_hole(*mem, size);
            break;
        case 'W':
            hole = find_worst_fit_hole(*mem, size);
            break;
        default:
            return E_FLAG;
    }

    // 5. allocate in chosen hole and install pid
    if (NULL != hole)
    {
        new = (region *)malloc(sizeof(region));
        if (NULL == new) return -1;
        if (hole == *mem) *mem = new;
        int ret = allocate(new, hole, pid, size);
        if (0 != ret)
        {
            if (NULL != hole && 0 == hole->pos.start) *mem = hole;
            free(new);
            return ret;
        }
        return install(pid, new);
    }

    // 6. no memory available
    return E_MEM;
}
int release_reg (unsigned long pid)
{
    // 1. find owning proc and region
    proc *existing = lookup(pid);
    if (NULL == existing) return E_NOTDEF;
    region *reg = existing->reg;

    // 2. remove from proc table
    int ret = undef(pid);
    if (0 != ret) return ret;

    // 3. turn region into a hole
    reg->kind = R_HOLE;
    reg->pid = 0;

    // 4. merge with neighboring holes
    if (NULL != reg->next && R_HOLE == reg->next->kind)
        ret = combine(reg, reg->next);
    if (NULL != reg->prev && R_HOLE == reg->prev->kind)
        ret = combine(reg->prev, reg);
    
    return ret;
}
int compact_regs (region **mem)
{
    // 1. walk regions
    region *reg = *mem;
    while (NULL != reg)
    {
        if (R_HOLE != reg->kind) 
        {
            // 2. skip processes
            reg = reg->next;
            continue;
        }

        // 3. bubble hole past consecutive processes
        while (NULL != reg->next && R_PROC == reg->next->kind)
        {
            if (*mem == reg) *mem = reg->next;
            
            // 4. update numbers
            unsigned long tmp = reg->pos.start;
            reg->pos.start += reg->next->pos.size;
            reg->next->pos.start = tmp;

            // 5. update connections
            reg->next->prev = reg->prev;
            if (NULL != reg->prev) reg->prev->next = reg->next;
            reg->prev = reg->next;
            reg->next = reg->next->next;
            if (NULL != reg->next) reg->next->prev = reg;
            reg->prev->next = reg;
        }

        // 6. merge adjacent holes if any
        if (NULL != reg->next)
        {
            int ret = combine(reg, reg->next);
            if (0 != ret) return ret;
        }

        // 7. reached end
        else reg = reg->next;
    }

    return 0;
}
void stat(region *mem)
{
    for (region *reg = mem; NULL != reg; reg = reg->next)
    {
        unsigned long start = reg->pos.start;
        unsigned long end = reg->pos.start + reg->pos.size - 1;

        if (R_PROC == reg->kind) printf("Addresses [%lu:%lu] Process P%lu\n", start, end, reg->pid);
        else printf("Addresses [%lu:%lu] Unused\n", start, end);
    }
}
int main (int argc, char **argv)
{
    region *mem = NULL;

    // 1. validate argument count
    if (2 != argc)
    {
        fprintf(stderr, "Usage: %s <size>\n", argv[0]);
        return 2;
    }

    // 2. parse total memory size
    char *endp = NULL;
    unsigned long total_size = strtoul(argv[1], &endp, 10);
    if ('\0' == argv[1][0] || (endp && '\0' != *endp))
    {
        fprintf(stderr, "Invalid size: %s\n", argv[1]);
        return 2;
    }
    if (0 == total_size)
    {
        fprintf(stderr, "Size must be > 0\n");
        return 2;
    }

    // 3. allocate and init initial region
    mem = (region *)malloc(sizeof(region));
    if (NULL == mem)
    {
        fprintf(stderr, "Failed to allocate initial region struct\n");
        return 1;
    }
    mem_init(mem, total_size);
    char buf[MAX_LINE];

    // 4. command loop
    for (;;)
    {
        printf("allocator> ");
        fflush(stdout);

        if (NULL == fgets(buf, MAX_LINE, stdin)) break;

        char *line = buf;
        while (*line && isspace((unsigned char)*line)) line++;
        if ('\0' == *line) continue;

        char cmd[16];
        if (1 != sscanf(line, "%15s", cmd)) continue;

        // RQ P<pid> <size> {F|B|W}
        if (0 == strcmp(cmd, "RQ"))
        {
            char pidstr[32];
            unsigned long req_size;
            char flag;

            if (3 != sscanf(line, "%*s %31s %lu %c", pidstr, &req_size, &flag))
            {
                fprintf(stderr, "Usage: RQ P<pid> <size> {F|B|W}\n");
                continue;
            }

            // allow P123 or just 123
            char *pidp = pidstr;
            if ('P' == pidp[0] || 'p' == pidp[0]) pidp++;

            char *pend = NULL;
            unsigned long pid = strtoul(pidp, &pend, 10);
            if ('\0' == pidp[0] || (pend && '\0' != *pend))
            {
                fprintf(stderr, "Invalid pid: %s\n", pidstr);
                continue;
            }

            int rc = request_reg(&mem, pid, req_size, flag);
            if (0 != rc) fprintf(stderr, "RQ error (code %d)\n", rc);
        }
        
        // RL P<pid>
        else if (0 == strcmp(cmd, "RL"))
        {
            char pidstr[32];

            if (1 != sscanf(line, "%*s %31s", pidstr))
            {
                fprintf(stderr, "Usage: RL P<pid>\n");
                continue;
            }

            char *pidp = pidstr;
            if ('P' == pidp[0] || 'p' == pidp[0]) pidp++;

            char *pend = NULL;
            unsigned long pid = strtoul(pidp, &pend, 10);
            if ('\0' == pidp[0] || (pend && '\0' != *pend))
            {
                fprintf(stderr, "Invalid pid: %s\n", pidstr);
                continue;
            }

            int rc = release_reg(pid);
            if (0 != rc) fprintf(stderr, "RL error (code %d)\n", rc);
        }

        // C
        else if (0 == strcmp(cmd, "C"))
        {
            int rc = compact_regs(&mem);
            if (0 != rc) fprintf(stderr, "C error (code %d)\n", rc);
        }

        // STAT
        else if (0 == strcmp(cmd, "STAT"))
        {
            stat(mem);
        }

        // X
        else if (0 == strcmp(cmd, "X") || 0 == strcmp(cmd, "x"))
        {
            break;
        }

        else
        {
            fprintf(stderr, "Unknown command: %s\n", cmd);
        }
    }

    return 0;
}
