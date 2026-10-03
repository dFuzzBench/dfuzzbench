/*
 * Copyright (c) 2007,2008,2009,2010 Mij <mij@bitchx.it>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


/*
 * SimCList library. See http://mij.oltrelinux.com/devel/simclist
 */

/* SimCList implementation, version 1.5, with local modifications */

#include <stdlib.h>
#include <string.h>
#include <errno.h>      /* for setting errno */
#include <sys/types.h>
#if !defined(_WIN32)
#include <arpa/inet.h>  /* for htons() */
#include <unistd.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>   /* for gettimeofday() */
#endif
#include <stdint.h>
#else
#include <winsock2.h>
#endif
#ifdef SIMCLIST_DUMPRESTORE
#ifndef _WIN32
#include <sys/uio.h>    /* for READ_ERRCHECK() and write() */
#endif
#include <fcntl.h>      /* for open() etc */
#endif
#include <time.h>       /* for time() for random seed */
#include <sys/stat.h>   /* for open()'s access modes S_IRUSR etc */
#include <limits.h>


#ifdef SIMCLIST_DUMPRESTORE
/* convert 64bit integers from host to network format */
#define hton64(x)       (\
        htons(1) == 1 ?                                         \
            (uint64_t)x      /* big endian */                   \
        :       /* little endian */                             \
        ((uint64_t)((((uint64_t)(x) & 0xff00000000000000ULL) >> 56) | \
            (((uint64_t)(x) & 0x00ff000000000000ULL) >> 40) | \
            (((uint64_t)(x) & 0x0000ff0000000000ULL) >> 24) | \
            (((uint64_t)(x) & 0x000000ff00000000ULL) >>  8) | \
            (((uint64_t)(x) & 0x00000000ff000000ULL) <<  8) | \
            (((uint64_t)(x) & 0x0000000000ff0000ULL) << 24) | \
            (((uint64_t)(x) & 0x000000000000ff00ULL) << 40) | \
            (((uint64_t)(x) & 0x00000000000000ffULL) << 56)))   \
        )

/* convert 64bit integers from network to host format */
#define ntoh64(x)       (hton64(x))
#endif

/* some OSes don't have EPROTO (eg OpenBSD) */
#ifndef EPROTO
#define EPROTO  EIO
#endif

/* disable asserts */
#ifndef SIMCLIST_DEBUG
#ifndef NDEBUG
#define NDEBUG
#endif
#endif

#include <assert.h>

#ifdef SIMCLIST_WITH_THREADS
/* limit (approx) to the number of threads running
 * for threaded operations. Only meant when
 * SIMCLIST_WITH_THREADS is defined */
#define SIMCLIST_MAXTHREADS   2
#endif

/*
 * how many elems to keep as spare. During a deletion, an element
 * can be saved in a "free-list", not free()d immediately. When
 * latter insertions are performed, spare elems can be used instead
 * of malloc()ing new elems.
 *
 * about this param, some values for appending
 * 10 million elems into an empty list:
 * (#, time[sec], gain[%], gain/no[%])
 * 0    2,164   0,00    0,00    <-- feature disabled
 * 1    1,815   34,9    34,9
 * 2    1,446   71,8    35,9    <-- MAX gain/no
 * 3    1,347   81,7    27,23
 * 5    1,213   95,1    19,02
 * 8    1,064   110,0   13,75
 * 10   1,015   114,9   11,49   <-- MAX gain w/ likely sol
 * 15   1,019   114,5   7,63
 * 25   0,985   117,9   4,72
 * 50   1,088   107,6   2,15
 * 75   1,016   114,8   1,53
 * 100  0,988   117,6   1,18
 * 150  1,022   114,2   0,76
 * 200  0,939   122,5   0,61    <-- MIN time
 */
#ifndef SIMCLIST_MAX_SPARE_ELEMS
#define SIMCLIST_MAX_SPARE_ELEMS        5
#endif


#ifdef SIMCLIST_WITH_THREADS
#include <pthread.h>
#endif

#include "simclist.h"


/* minimum number of elements for sorting with quicksort instead of insertion */
#define SIMCLIST_MINQUICKSORTELS        24


/* list dump declarations */
#define SIMCLIST_DUMPFORMAT_VERSION     1   /* (short integer) version of fileformat managed by _dump* and _restore* functions */

#define SIMCLIST_DUMPFORMAT_HEADERLEN   30  /* length of the header */

/* header for a list dump */
struct list_dump_header_s {
    uint16_t ver;               /* version */
    int64_t timestamp;          /* dump timestamp */
    int32_t rndterm;            /* random value terminator -- terminates the data sequence */

    uint32_t totlistlen;        /* sum of every element' size, bytes */
    uint32_t numels;            /* number of elements */
    uint32_t elemlen;           /* bytes length of an element, for constant-size lists, <= 0 otherwise */
    int32_t listhash;           /* hash of the list at the time of dumping, or 0 if to be ignored */
};



/* deletes tmp from list, with care wrt its position (head, tail, middle) */
static int list_drop_elem(list_t *simclist_restrict l, struct list_entry_s *tmp, unsigned int pos);

/* set default values for initialized lists */
static int list_attributes_setdefaults(list_t *simclist_restrict l);

#ifndef NDEBUG
/* check whether the list internal REPresentation is valid -- Costs O(n) */
static int list_repOk(const list_t *simclist_restrict l);

/* check whether the list attribute set is valid -- Costs O(1) */
static int list_attrOk(const list_t *simclist_restrict l);
#endif

/* do not inline, this is recursive */
static void list_sort_quicksort(list_t *simclist_restrict l, int versus,
        unsigned int first, struct list_entry_s *fel,
        unsigned int last, struct list_entry_s *lel);

static simclist_inline void list_sort_selectionsort(list_t *simclist_restrict l, int versus,
        unsigned int first, struct list_entry_s *fel,
        unsigned int last, struct list_entry_s *lel);

static void *list_get_minmax(const list_t *simclist_restrict l, int versus);

static simclist_inline struct list_entry_s *list_findpos(const list_t *simclist_restrict l, int posstart);

#ifdef SIMCLIST_DUMPRESTORE
/* write() decorated with error checking logic */
#define WRITE_ERRCHECK(fd, msgbuf, msglen)      do {                                                    \
                                                    if (write(fd, msgbuf, msglen) < 0) return -1;       \
                                                } while (0);
/* READ_ERRCHECK() decorated with error checking logic */
#define READ_ERRCHECK(fd, msgbuf, msglen)      do {                                                     \
                                                    if (read(fd, msgbuf, msglen) != msglen) {           \
                                                        /*errno = EPROTO;*/                             \
                                                        free(buf);                                      \
                                                        return -1;                                      \
                                                    }                                                   \
                                                } while (0);
#endif

/*
 * Random Number Generator
 *
 * The user is expected to seed the RNG (ie call srand()) if
 * SIMCLIST_SYSTEM_RNG is defined.
 *
 * Otherwise, a self-contained RNG based on LCG is used; see
 * http://en.wikipedia.org/wiki/Linear_congruential_generator .
 *
 * Facts pro local RNG:
 * 1. no need for the user to call srand() on his own
 * 2. very fast, possibly faster than OS
 * 3. avoid interference with user's RNG
 *
 * Facts pro system RNG:
 * 1. may be more accurate (irrelevant for SimCList random purposes)
 * 2. why reinvent the wheel
 *
 * Default to local RNG for user's ease of use.
 */

#ifdef SIMCLIST_SYSTEM_RNG
/* keep track whether we initialized already (non-0) or not (0) */
static unsigned random_seed = 0;

/* use local RNG */
static simclist_inline void seed_random() {
    if (random_seed == 0)
        random_seed = (unsigned)getpid() ^ (unsigned)time(NULL);
}

static simclist_inline long get_random() {
    random_seed = (1664525 * random_seed + 1013904223);
    return random_seed;
}

#else
/* use OS's random generator */
#   define  seed_random()
#   define  get_random()        (rand())
#endif


/* list initialization */
int list_init(list_t *simclist_restrict l) {
    if (l == NULL) {
        return -1;
    }

    memset(l, 0, sizeof *l);

    seed_random();

    l->numels = 0;

    /* head/tail sentinels and mid pointer */
    l->head_sentinel = (struct list_entry_s *)malloc(sizeof(struct list_entry_s));
    l->tail_sentinel = (struct list_entry_s *)malloc(sizeof(struct list_entry_s));
    if (l->tail_sentinel == NULL || l->head_sentinel == NULL) {
        return -1;
    }
    l->head_sentinel->next = l->tail_sentinel;
    l->tail_sentinel->prev = l->head_sentinel;
    l->head_sentinel->prev = l->tail_sentinel->next = l->mid = NULL;
    l->head_sentinel->data = l->tail_sentinel->data = NULL;

    /* iteration attributes */
    l->iter_active = 0;
    l->iter_pos = 0;
    l->iter_curentry = NULL;

    /* free-list attributes */
    l->spareelsnum = 0;
    l->spareels = (struct list_entry_s **)malloc(SIMCLIST_MAX_SPARE_ELEMS * sizeof(struct list_entry_s *));
    if (l->spareels == NULL) {
        return -1;
    }

#ifdef SIMCLIST_WITH_THREADS
    l->threadcount = 0;
#endif

    if (0 != list_attributes_setdefaults(l)) {
        return -1;
    }

    assert(list_repOk(l));
    assert(list_attrOk(l));

    return 0;
}

void list_destroy(list_t *simclist_restrict l) {
    unsigned int i;

    list_clear(l);
    for (i = 0; i < l->spareelsnum; i++) {
        free(l->spareels[i]);
    }
    free(l->spareels);
    free(l->head_sentinel);
    free(l->tail_sentinel);
}

int list_attributes_setdefaults(list_t *simclist_restrict l) {
    l->attrs.comparator = NULL;
    l->attrs.seeker = NULL;

    /* also free() element data when removing and element from the list */
    l->attrs.meter = NULL;
    l->attrs.copy_data = 0;

    l->attrs.hasher = NULL;

    /* serializer/unserializer */
    l->attrs.serializer = NULL;
    l->attrs.unserializer = NULL;

    assert(list_attrOk(l));

    return 0;
}

/* setting list properties */
int list_attributes_comparator(list_t *simclist_restrict l, element_comparator comparator_fun) {
    if (l == NULL) return -1;

    l->attrs.comparator = comparator_fun;

    assert(list_attrOk(l));

    return 0;
}

int list_attributes_seeker(list_t *simclist_restrict l, element_seeker seeker_fun) {
    if (l == NULL) return -1;

    l->attrs.seeker = seeker_fun;
    assert(list_attrOk(l));

    return 0;
}

int list_attributes_copy(list_t *simclist_restrict l, element_meter metric_fun, int copy_data) {
    if (l == NULL || (metric_fun == NULL && copy_data != 0)) return -1;

    l->attrs.meter = metric_fun;
    l->attrs.copy_data = copy_data;

    assert(list_attrOk(l));

    return 0;
}

int list_attributes_hash_computer(list_t *simclist_restrict l, element_hash_computer hash_computer_fun) {
    if (l == NULL) return -1;

    l->attrs.hasher = hash_computer_fun;
    assert(list_attrOk(l));
    return 0;
}

int list_attributes_serializer(list_t *simclist_restrict l, element_serializer serializer_fun) {
    if (l == NULL) return -1;

    l->attrs.serializer = serializer_fun;
    assert(list_attrOk(l));
    return 0;
}

int list_attributes_unserializer(list_t *simclist_restrict l, element_unserializer unserializer_fun) {
    if (l == NULL) return -1;

    l->attrs.unserializer = unserializer_fun;
    assert(list_attrOk(l));
    return 0;
}

int list_append(list_t *simclist_restrict l, const void *data) {
    return list_insert_at(l, data, l->numels);
}

int list_prepend(list_t *simclist_restrict l, const void *data) {
    return list_insert_at(l, data, 0);
}

void *list_fetch(list_t *simclist_restrict l) {
    return list_extract_at(l, 0);
}

void *list_get_at(const list_t *simclist_restrict l, unsigned int pos) {
    struct list_entry_s *tmp;

    tmp = list_findpos(l, pos);

    return (tmp != NULL ? tmp->data : NULL);
}

void *list_get_max(const list_t *simclist_restrict l) {
    return list_get_minmax(l, +1);
}

void *list_get_min(const list_t *simclist_restrict l) {
    return list_get_minmax(l, -1);
}

/* REQUIRES {list->numels >= 1}
 * return the min (versus < 0) or max value (v > 0) in l */
static void *list_get_minmax(const list_t *simclist_restrict l, int versus) {
    void *curminmax;
    struct list_entry_s *s;

    if (l->attrs.comparator == NULL || l->numels == 0)
        return NULL;

    curminmax = l->head_sentinel->next->data;
    for (s = l->head_sentinel->next->next; s != l->tail_sentinel; s = s->next) {
        if (l->attrs.comparator(curminmax, s->data) * versus > 0)
            curminmax = s->data;
    }

    return curminmax;
}

/* set tmp to point to element at index posstart in l */
static simclist_inline struct list_entry_s *list_findpos(const list_t *simclist_restrict l, int posstart) {
    struct list_entry_s *ptr;
    float x;
    int i;

    if (l->head_sentinel == NULL || l->tail_sentinel == NULL) return NULL;

    /* accept 1 slot overflow for fetching head and tail sentinels */
    if (posstart < -1 || posstart > (int)l->numels) return NULL;

    x = l->numels ? (float)(posstart+1) / l->numels : 0;
    if (x <= 0.25) {
        /* first quarter: get to posstart from head */
        for (i = -1, ptr = l->head_sentinel; i < posstart; ptr = ptr->next, i++);
    } else if (x < 0.5) {
        /* second quarter: get to posstart from mid */
        for (i = (l->numels-1)/2, ptr = l->mid; i > posstart; ptr = ptr->prev, i--);
    } else if (x <= 0.75) {
        /* third quarter: get to posstart from mid */
        for (i = (l->numels-1)/2, ptr = l->mid; i < posstart; ptr = ptr->next, i++);
    } else {
        /* fourth quarter: get to posstart from tail */
        for (i = l->numels, ptr = l->tail_sentinel; i > posstart; ptr = ptr->prev, i--);
    }

    return ptr;
}

void *list_extract_at(list_t *simclist_restrict l, unsigned int pos) {
    struct list_entry_s *tmp;
    void *data;

    if (l->iter_active || pos >= l->numels) return NULL;

    tmp = list_findpos(l, pos);
    if (tmp == NULL) {
        return NULL;
    }
    data = tmp->data;

    tmp->data = NULL;   /* save data from list_drop_elem() free() */
    list_drop_elem(l, tmp, pos);
    l->numels--;

    assert(list_repOk(l));

    return data;
}

int list_insert_at(list_t *simclist_restrict l, const void *data, unsigned int pos) {
    struct list_entry_s *lent, *succ, *prec;

    if (l->iter_active || pos > l->numels) return -1;

    /* this code optimizes malloc() with a free-list */
    if (l->spareelsnum > 0) {
        lent = l->spareels[l->spareelsnum-1];
        l->spareelsnum--;
    } else {
        lent = (struct list_entry_s *)malloc(sizeof(struct list_entry_s));
        if (lent == NULL) {
            return -1;
        }
    }

    if (l->attrs.copy_data) {
        /* make room for user' data (has to be copied) */
        size_t datalen = l->attrs.meter(data);
        lent->data = (struct list_entry_s *)malloc(datalen);
        if (lent->data == NULL) {
            if (!(l->spareelsnum > 0)) {
                free(lent);
            }
            return -1;
        }
        memcpy(lent->data, data, datalen);
    } else {
        lent->data = (void*)data;
    }

    /* actually append element */
    prec = list_findpos(l, pos-1);
    if (prec == NULL) {
        if (l->attrs.copy_data) {
            free(lent->data);
        }
        if (!(l->spareelsnum > 0)) {
            free(lent);
        }
        return -1;
    }
    succ = prec->next;

    prec->next = lent;
    lent->prev = prec;
    lent->next = succ;
    succ->prev = lent;

    l->numels++;

    /* fix mid pointer */
    if (l->numels == 1) { /* first element, set pointer */
        l->mid = lent;
    } else if (l->numels % 2) {    /* now odd */
        if (pos >= (l->numels-1)/2) l->mid = l->mid->next;
    } else {                /* now even */
        if (pos <= (l->numels-1)/2) l->mid = l->mid->prev;
    }

    assert(list_repOk(l));

    return 1;
}

int list_delete(list_t *simclist_restrict l, const void *data) {
	int pos, r;

	pos = list_locate(l, data);
	if (pos < 0)
		return -1;

	r = list_delete_at(l, pos);
	if (r < 0)
		return -1;

    assert(list_repOk(l));

	return 0;
}

int list_delete_at(list_t *simclist_restrict l, unsigned int pos) {
    struct list_entry_s *delendo;


    if (l->iter_active || pos >= l->numels) return -1;

    delendo = list_findpos(l, pos);

    list_drop_elem(l, delendo, pos);

    l->numels--;


    assert(list_repOk(l));

    return  0;
}

int list_delete_range(list_t *simclist_restrict l, unsigned int posstart, unsigned int posend) {
    struct list_entry_s *lastvalid, *tmp, *tmp2;
    unsigned int i;
    int movedx;
    unsigned int numdel, midposafter;

    if (l->iter_active || posend < posstart || posend >= l->numels) return -1;

