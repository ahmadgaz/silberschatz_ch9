#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_FRAMES 50
#define MAX_REF_LEN 1000
#define DEFAULT_REF_LEN 20
#define PAGE_MIN 0
#define PAGE_MAX 9

typedef struct {
    int frames[MAX_FRAMES];
    int next;
    int faults;
} FIFOState;

typedef struct {
    int frames[MAX_FRAMES];
    int last_used[MAX_FRAMES];
    int faults;
} LRUState;

typedef struct {
    int frames[MAX_FRAMES];
    int faults;
} OPTState;

/* init all frame slots to empty (-1) */
static void init_frames(int frames[], int n)
{
    for (int i = 0; i < n; i++)
        frames[i] = -1;
}

/* find page in frames, return index or -1 */
static int find_page(int frames[], int n, int page)
{
    for (int i = 0; i < n; i++)
        if (frames[i] == page)
            return i;
    return -1;
}

/* print frames as digits (or '.') plus 'F' or '.' for fault */
static void print_column(int frames[], int n, int fault)
{
    for (int i = 0; i < n; i++) {
        if (frames[i] < 0)
            putchar('.');
        else
            printf("%d", frames[i]);
    }
    if (fault)
        putchar('F');
    else
        putchar('.');
}

static int fifo_step(FIFOState *s, int nframes, int page)
{
    int i;

    /* hit? */
    if (find_page(s->frames, nframes, page) != -1)
        return 0;

    /* miss */
    s->faults++;

    /* free frame? */
    for (i = 0; i < nframes; i++) {
        if (s->frames[i] == -1) {
            s->frames[i] = page;
            return 1;
        }
    }

    /* no free, replace fifo_next */
    s->frames[s->next] = page;
    s->next = (s->next + 1) % nframes;

    return 1;
}

static int lru_step(LRUState *s, int nframes, int page, int time)
{
    int i;

    /* hit? update last_used */
    int idx = find_page(s->frames, nframes, page);
    if (idx != -1) {
        s->last_used[idx] = time;
        return 0;
    }

    /* miss */
    s->faults++;

    /* free frame? */
    for (i = 0; i < nframes; i++) {
        if (s->frames[i] == -1) {
            s->frames[i]     = page;
            s->last_used[i]  = time;
            return 1;
        }
    }

    /* choose least recently used */
    int victim = 0;
    int oldest_time = s->last_used[0];

    for (i = 1; i < nframes; i++) {
        if (s->last_used[i] < oldest_time) {
            oldest_time = s->last_used[i];
            victim = i;
        }
    }

    s->frames[victim]    = page;
    s->last_used[victim] = time;

    return 1;
}

static int opt_step(OPTState *s,
                    int nframes,
                    int page,
                    int pos,          /* current index in ref string */
                    int refs[],
                    int ref_len)
{
    int i;

    /* hit? */
    if (find_page(s->frames, nframes, page) != -1)
        return 0;

    /* miss */
    s->faults++;

    /* free frame? */
    for (i = 0; i < nframes; i++) {
        if (s->frames[i] == -1) {
            s->frames[i] = page;
            return 1;
        }
    }

    /* find victim: page with farthest next use (or never used again) */
    int victim = 0;
    int farthest = -1;

    for (i = 0; i < nframes; i++) {
        int p = s->frames[i];
        int next_use = -1;

        for (int j = pos + 1; j < ref_len; j++) {
            if (refs[j] == p) {
                next_use = j;
                break;
            }
        }

        if (next_use == -1) {        /* never used again */
            victim = i;
            farthest = ref_len + 1;  /* bigger than any index */
            break;
        }

        if (next_use > farthest) {
            farthest = next_use;
            victim = i;
        }
    }

    s->frames[victim] = page;

    return 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s <num_frames> [ref_len] [seed]\n", argv[0]);
        fprintf(stderr, "  num_frames : 1..%d\n", MAX_FRAMES);
        fprintf(stderr, "  ref_len    : 1..%d (default %d)\n",
                MAX_REF_LEN, DEFAULT_REF_LEN);
        fprintf(stderr, "  seed       : unsigned int (default time)\n");
        return 1;
    }

    int nframes = atoi(argv[1]);
    if (nframes <= 0 || nframes > MAX_FRAMES) {
        fprintf(stderr, "num_frames must be between 1 and %d\n", MAX_FRAMES);
        return 1;
    }

    int ref_len = DEFAULT_REF_LEN;
    if (argc >= 3) {
        ref_len = atoi(argv[2]);
        if (ref_len <= 0 || ref_len > MAX_REF_LEN) {
            fprintf(stderr, "ref_len must be between 1 and %d\n", MAX_REF_LEN);
            return 1;
        }
    }

    unsigned int seed;
    if (argc == 4) {
        seed = (unsigned int)strtoul(argv[3], NULL, 10);
    } else {
        seed = (unsigned int)time(NULL);
    }

    int refs[MAX_REF_LEN];

    srand(seed);

    for (int i = 0; i < ref_len; i++)
        refs[i] = PAGE_MIN + rand() % (PAGE_MAX - PAGE_MIN + 1);

    printf("page frames  : %d\n", nframes);
    printf("ref length   : %d\n", ref_len);
    printf("random seed  : %u\n", seed);
    printf("ref string   : ");
    for (int i = 0; i < ref_len; i++)
        printf("%d ", refs[i]);
    printf("\n\n");

    /* init algorithm states */
    FIFOState fifo;
    LRUState  lru;
    OPTState  opt;

    init_frames(fifo.frames, nframes);
    fifo.next   = 0;
    fifo.faults = 0;

    init_frames(lru.frames, nframes);
    for (int i = 0; i < nframes; i++)
        lru.last_used[i] = -1;
    lru.faults = 0;

    init_frames(opt.frames, nframes);
    opt.faults = 0;

    /* table header */
    printf("Ref  FIFO   LRU    OPT\n");
    printf("--------------------------\n");

    /* simulate all three in lockstep */
    for (int t = 0; t < ref_len; t++) {
        int page = refs[t];

        int f_fault = fifo_step(&fifo, nframes, page);
        int l_fault = lru_step(&lru, nframes, page, t);
        int o_fault = opt_step(&opt, nframes, page, t, refs, ref_len);

        printf("%3d  ", page);
        print_column(fifo.frames, nframes, f_fault);
        printf("   ");
        print_column(lru.frames, nframes, l_fault);
        printf("   ");
        print_column(opt.frames, nframes, o_fault);
        printf("\n");
    }

    printf("--------------------------\n");
    printf("faults FIFO = %d\n", fifo.faults);
    printf("faults LRU  = %d\n", lru.faults);
    printf("faults OPT  = %d\n", opt.faults);

    return 0;
}
