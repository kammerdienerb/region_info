#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <linux/perf_event.h>
#include <asm/perf_regs.h>
#include <asm/unistd.h>
#include <perfmon/pfmlib_perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "tree.h"
#include "hash_table.h"

int omp_get_num_threads(void);


/* #define ASSERT(cond) ; */
#define ASSERT(cond) assert((cond))


/******************************************************************************
 * BEGIN:                                                   Allocation Tracking
 ******************************************************************************/

typedef void *ptr_t;
use_tree(ptr_t, uint64_t);

static tree(ptr_t, uint64_t)  alloc_tree;
static void                  *chunks_end;
pthread_rwlock_t              chunks_end_rw_lock;
pthread_rwlock_t              at_rw_lock;

#define CE_RLOCK() \
    do { pthread_rwlock_rdlock(&chunks_end_rw_lock); }    while (0)
#define CE_WLOCK() \
    do { pthread_rwlock_wrlock(&chunks_end_rw_lock); }    while (0)
#define CE_UNLOCK()\
    do { pthread_rwlock_unlock(&chunks_end_rw_lock); }    while (0)

#define AT_RLOCK() \
    do { pthread_rwlock_rdlock(&at_rw_lock); }            while (0)
#define AT_WLOCK() \
    do { pthread_rwlock_wrlock(&at_rw_lock); }            while (0)
#define AT_UNLOCK()\
    do { pthread_rwlock_unlock(&at_rw_lock); }            while (0)

/******************************************************************************
 * END:                                                     Allocation Tracking
 ******************************************************************************/

/******************************************************************************
 * BEGIN:                                                  Thread-Local Storage
 ******************************************************************************/

typedef struct {
    /* thread-local data here */
} thread_local_info;

static int                 max_threads             = 4096;
static thread_local_info **thread_local_infos      = NULL;
static int                 n_thread_local_infos    = 0;
static pthread_mutex_t     thread_local_info_lock;
static pthread_key_t       thread_local_info_key;

static void thread_local_info_init(thread_local_info *info) { }
static void thread_local_info_fini(thread_local_info *info) { }

static thread_local_info* get_thread_local_struct() {
    thread_local_info *info;
    int                idx;
    
    info = pthread_getspecific(thread_local_info_key);

    if (info == NULL) {
        pthread_mutex_lock(&thread_local_info_lock);
        idx  = n_thread_local_infos++;
        info = malloc(sizeof(*info));
        pthread_setspecific(thread_local_info_key, info);
        thread_local_info_init(info);
        thread_local_infos[idx] = info;
        pthread_mutex_unlock(&thread_local_info_lock);
    }

    return info;
}

/******************************************************************************
 * END:                                                    Thread-Local Storage
 ******************************************************************************/

/******************************************************************************
 * BEGIN:                                             Parallel Region Profiling
 ******************************************************************************/

pthread_mutex_t access_profile_flush_mutex;
pthread_mutex_t access_profile_flush_signal_mutex;

#define MAX_PARALLEL_REGION_STACK_DEPTH (128)

static inline uint64_t ull_identity_hash(uint64_t u)    { return u; }

use_hash_table(uint64_t, uint64_t);

typedef struct {
    uint64_t                       id;
    int                            concurrency;
    int                            in_stack;
    pthread_mutex_t                mtx;
    hash_table(uint64_t, uint64_t) site_reads;
} *parallel_region_info;

#define RI_LOCK(info) \
    do { pthread_mutex_lock(&info->mtx);   }    while (0)
#define RI_UNLOCK(info)\
    do { pthread_mutex_unlock(&info->mtx); }    while (0)

use_tree(uint64_t, parallel_region_info);

static tree(uint64_t, parallel_region_info) region_tree;
pthread_rwlock_t                            region_tree_rw_lock;

#define RT_RLOCK() \
    do { pthread_rwlock_rdlock(&region_tree_rw_lock); }    while (0)
#define RT_WLOCK() \
    do { pthread_rwlock_wrlock(&region_tree_rw_lock); }    while (0)
#define RT_UNLOCK()\
    do { pthread_rwlock_unlock(&region_tree_rw_lock); }    while (0)


parallel_region_info parallel_region_info_get_or_make(uint64_t id) {
    tree_it(uint64_t, parallel_region_info) it;
    parallel_region_info                    info;

    info = NULL;

    RT_RLOCK(); {
        it = tree_lookup(region_tree, id);
        if (tree_it_good(it)) {
            info = tree_it_val(it);
        }
    } RT_UNLOCK();

    if (info == NULL) {
        RT_WLOCK(); {
            info = malloc(sizeof(*info));
            info->id          = id;
            info->concurrency = 0;
            info->in_stack    = 0;
            info->site_reads  = hash_table_make(uint64_t, uint64_t, ull_identity_hash);
            pthread_mutex_init(&info->mtx, NULL);
            tree_insert(region_tree, id, info);
        } RT_UNLOCK();
    }

    return info;
}


typedef struct {
    parallel_region_info   *data;
    uint64_t  used;
} parallel_region_stack;

static parallel_region_stack region_info_stack;
pthread_rwlock_t             region_info_stack_rw_lock;

#define RS_RLOCK() \
    do { pthread_rwlock_rdlock(&region_info_stack_rw_lock); }    while (0)
#define RS_WLOCK() \
    do { pthread_rwlock_wrlock(&region_info_stack_rw_lock); }    while (0)
#define RS_UNLOCK()\
    do { pthread_rwlock_unlock(&region_info_stack_rw_lock); }    while (0)


parallel_region_stack parallel_region_stack_make() {
    parallel_region_stack stack;

    stack.data = malloc(sizeof(parallel_region_info) * MAX_PARALLEL_REGION_STACK_DEPTH);
    stack.used = 0;

    return stack;
}

parallel_region_info parallel_region_stack_top(parallel_region_stack *stack) {
    if (stack->used == 0)    { return NULL; }

    return stack->data[stack->used - 1];
}

parallel_region_info parallel_region_stack_push(parallel_region_stack *stack, parallel_region_info info) {
    return (stack->data[stack->used++] = info);
}

parallel_region_info parallel_region_stack_pop(parallel_region_stack *stack) {
    ASSERT(stack->used > 0 && "pop() on empty stack");         
    
    return stack->data[--stack->used];
}

/*******************************************************************************
 * END:                                                Parallel Region Profiling
 *******************************************************************************/


/******************************************************************************
 * BEGIN:                                                      Access Profiling
 ******************************************************************************/

struct __attribute__ ((__packed__)) sample {
    uint64_t addr;
};

typedef struct {
    pthread_mutex_t              mtx;
    pthread_t                    profile_id;

    size_t                       pagesize;

    /* For perf */
    size_t                       size,
                                 total;
    struct perf_event_attr      *pe;
    struct perf_event_mmap_page *metadata;
    int                          fd;
    uint64_t                     consumed;
    struct pollfd                pfd;
    char                         oops;

    /* For libpfm */
    pfm_perf_encode_arg_t       *pfm;
} profile_info;


static hash_table(uint64_t, uint64_t) all_site_reads, tmp_site_reads;
profile_info                          prof;

const char* accesses_event_strs[] = {
    "MEM_LOAD_UOPS_RETIRED.L3_MISS",
    "MEM_UOPS_RETIRED.L2_MISS_LOADS",
    NULL
};

int   sample_freq      = 2048;
int   max_sample_pages = 64;
float profile_rate     = 0.5;

static int region_info_should_stop() {
    switch(pthread_mutex_trylock(&prof.mtx)) {
        case 0:
            pthread_mutex_unlock(&prof.mtx);
            return 1;
        case EBUSY:
            return 0;
    }
    return 1;
}

static int region_info_do_poll() {
    prof.pfd.fd = prof.fd;
    prof.pfd.events = POLLIN;
    prof.pfd.revents = 0;
    return poll(&prof.pfd, 1, 0);
}

static void add_sample_to_hash_table(hash_table(uint64_t, uint64_t) table, uint64_t id) {
    uint64_t *n_reads;

    n_reads = hash_table_get_val(table, id); 
    if (n_reads) {
        *n_reads += 1;
    } else {
        hash_table_insert(table, id, 1);
    }
}

static void region_info_get_accesses(hash_table(uint64_t, uint64_t) region_table) {
    uint64_t                  head, tail, buf_size, id, *n_reads, tmp_id, *tmp_n_reads;
    void                     *addr, *alloc_addr;
    char                     *base, *begin, *end;
    struct sample            *sample;
    struct perf_event_header *header;
    tree_it(ptr_t, uint64_t)  it;

    /* Get ready to read */
    head     = prof.metadata->data_head;
    tail     = prof.metadata->data_tail;
    buf_size = prof.pagesize * max_sample_pages;
    asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

    base  = (char *)prof.metadata + prof.pagesize;
    begin = base + tail % buf_size;
    end   = base + head % buf_size;

    /* Read all of the samples */
    while (begin != end) {
        header = (struct perf_event_header *)begin;
        if (header->size == 0) {
            break;
        }
        sample = (struct sample *) (begin + 8);
        addr   = (void *) (sample->addr);

        if (addr) {
            AT_RLOCK(); {
                it = tree_gtr(alloc_tree, addr);
                tree_it_prev(it);
                if (!tree_it_good(it)) {
                    AT_UNLOCK();
                    goto inc;
                }
                id         = tree_it_val(it);
                alloc_addr = tree_it_key(it);
            } AT_UNLOCK();

            add_sample_to_hash_table(all_site_reads, id);
            if (region_table == NULL) {
                add_sample_to_hash_table(tmp_site_reads, id);
            } else {
                add_sample_to_hash_table(region_table, id);
            }

            prof.total++;
        }
inc:
        /* Increment begin by the size of the sample */
        if (((char *)header + header->size) == base + buf_size) {
            begin = base;
        } else {
            begin = begin + header->size;
        }
    }

    if (region_table != NULL) {
        hash_table_traverse(tmp_site_reads, tmp_id, tmp_n_reads) {
            n_reads = hash_table_get_val(region_table, id); 
            if (n_reads) {
                *n_reads += 1;
            } else {
                hash_table_insert(region_table, id, 1);
            }
        }
        hash_table_free(tmp_site_reads);
        tmp_site_reads = hash_table_make(uint64_t, uint64_t, ull_identity_hash);
    }

    /* Let perf know that we've read this far */
    prof.metadata->data_tail = head;
    __sync_synchronize();
}

static void region_info_wait_and_get_accesses() {
    int err;

    /* Wait for the perf buffer to be ready */
    err = region_info_do_poll();
    if(err == 0) {
        return;
    } else if(err == -1) {
        fprintf(stderr, "Error occurred polling. Aborting.\n");
        exit(1);
    }

    region_info_get_accesses(NULL);
}

void* region_info_profile_fn(void *arg) {
    struct timespec timer;
    uint64_t site, *n_reads;
    parallel_region_info info;
    tree_it(uint64_t, parallel_region_info) it;

    /* mmap the file */
    prof.metadata = mmap(NULL, prof.pagesize + (prof.pagesize * max_sample_pages),
                         PROT_READ | PROT_WRITE, MAP_SHARED, prof.fd, 0);
    if (prof.metadata == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap room (%zu bytes) for perf samples. Aborting with:\n%s\n",
                prof.pagesize + (prof.pagesize * max_sample_pages), strerror(errno));
        exit(1);
    }

    /* Initialize */
    ioctl(prof.fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(prof.fd, PERF_EVENT_IOC_ENABLE, 0);
    prof.consumed = 0;
    prof.total    = 0;
    prof.oops     = 0;

    const uint64_t one_sec_in_ns = 1000000000;
    timer.tv_sec  = (uint64_t)profile_rate;
    timer.tv_nsec = (uint64_t)((profile_rate  - (float)timer.tv_sec) * one_sec_in_ns);

    while(!region_info_should_stop()) {
        /*
         * See if we've been signaled by a thread to dump our counts to the top
         * of the region stack.
         */
        if (pthread_mutex_trylock(&access_profile_flush_signal_mutex) == 0) {
            RS_RLOCK(); {
                info = parallel_region_stack_top(&region_info_stack);
            } RS_UNLOCK();
            /* ASSERT(info != NULL && "region info not found in stack -- can't dump"); */
            if (info != NULL) {
                region_info_get_accesses(info->site_reads);
            } else {
                region_info_get_accesses(NULL);
            }
            /* access_profile_flush_signal_mutex is unlocked
             * by another thread to trigger the flush */
        } else {
            region_info_wait_and_get_accesses();
            nanosleep(&timer, NULL);
        }
    }
    region_info_get_accesses(NULL);

    printf("*-BEG REGION INFO-*\n");
    printf("*-----------------*\n");
    hash_table_traverse(all_site_reads, site, n_reads) {
        printf("site %llu: %llu\n", site, *n_reads);
    }
    tree_traverse(region_tree, it) {
        printf("region %llu:\n", tree_it_key(it));
        hash_table_traverse(tree_it_val(it)->site_reads, site, n_reads) {
            printf("    site %llu: %llu\n", site, *n_reads);
        }
    }
    printf("*-END REGION INFO-*\n");
    printf("*-----------------*\n");
}

void region_info_get_event() {
    const char **event, *buf;
    int          err, found, i;

    pfm_initialize();
    prof.pfm = malloc(sizeof(pfm_perf_encode_arg_t));

    /* Iterate through the array of event strs and see which one works. 
     * For should_profile_one, just use the first given IMC. */
    event = accesses_event_strs;
    found = 0;
    while(*event != NULL) {
        memset(prof.pe, 0, sizeof(struct perf_event_attr));
        prof.pe->size = sizeof(struct perf_event_attr);
        memset(prof.pfm, 0, sizeof(pfm_perf_encode_arg_t));
        prof.pfm->size = sizeof(pfm_perf_encode_arg_t);
        prof.pfm->attr = prof.pe;
        err = pfm_get_os_event_encoding(*event, PFM_PLM2 | PFM_PLM3, PFM_OS_PERF_EVENT, prof.pfm);
        if(err == PFM_SUCCESS) {
            found = 1;
            break;
        }
        event++;
    }
    if(!found) {
        fprintf(stderr, "Couldn't find an appropriate event to use. Aborting.\n");
        exit(1);
    }

    prof.pe->sample_type    = PERF_SAMPLE_ADDR;
    prof.pe->sample_period  = sample_freq;
    prof.pe->mmap           = 1;
    prof.pe->disabled       = 1;
    prof.pe->exclude_kernel = 1;
    prof.pe->exclude_hv     = 1;
    prof.pe->precise_ip     = 2;
    prof.pe->task           = 1;
    prof.pe->sample_period  = sample_freq;
}

static void region_info_start_profile_thread() {
    size_t i;

    all_site_reads = hash_table_make(uint64_t, uint64_t, ull_identity_hash);
    tmp_site_reads = hash_table_make(uint64_t, uint64_t, ull_identity_hash);

    prof.pagesize = (size_t) sysconf(_SC_PAGESIZE);

    /* Allocate perf structs */
    prof.pe  = malloc(sizeof(struct perf_event_attr));
    prof.fd  = 0;

    /* Use libpfm to fill the pe struct */
    region_info_get_event();

    /* Open the perf file descriptor */
    prof.fd = syscall(__NR_perf_event_open, prof.pe, 0, -1, -1, 0);
    if (prof.fd == -1) {
        fprintf(stderr, "Error opening perf event 0x%llx.\n", prof.pe->config);
        strerror(errno);
        exit(EXIT_FAILURE);
    }

    /* Start the profiling threads */
    pthread_mutex_init(&prof.mtx, NULL);
    pthread_mutex_lock(&prof.mtx);
    pthread_create(&prof.profile_id, NULL, &region_info_profile_fn, NULL);
}

static void region_info_stop_profile_thread() {
    size_t i, associated;

    /* Stop the actual sampling */
    ioctl(prof.fd, PERF_EVENT_IOC_DISABLE, 0);

    /* Stop the timers and join the threads */
    pthread_mutex_unlock(&prof.mtx);
    pthread_join(prof.profile_id, NULL);

    close(prof.fd);

    printf("Done profiling.\n");
}
        
static void trigger_access_info_flush() {
    pthread_mutex_lock(&access_profile_flush_mutex);
        /* signal */ 
        pthread_mutex_unlock(&access_profile_flush_signal_mutex);
    pthread_mutex_unlock(&access_profile_flush_mutex);
}

/******************************************************************************
 * END:                                                        Access Profiling
 ******************************************************************************/

/******************************************************************************
 * BEGIN:                                                Runtime Implementation
 ******************************************************************************/

void region_info_init() __attribute__((constructor));
void region_info_fini() __attribute__((destructor));

void region_info_inst_alloc(void *ptr, size_t size, uint64_t id);
void region_info_inst_realloc(void *old_ptr, void *ptr, size_t size, uint64_t id);
void region_info_inst_dealloc(void *ptr);
void region_info_inst_read(void *ptr);
void region_info_inst_write(void *ptr);
void region_info_inst_parallel_enter();
void region_info_inst_parallel_exit();
void region_info_inst_parallel_count();

void region_info_init() {

    alloc_tree         = tree_make(ptr_t, uint64_t);
    region_tree        = tree_make(uint64_t, parallel_region_info);
    thread_local_infos = malloc(sizeof(thread_local_info*) * max_threads);
    memset(thread_local_infos, 0, sizeof(thread_local_info*) * max_threads);

    pthread_rwlock_init(&chunks_end_rw_lock,    NULL);
    pthread_rwlock_init(&at_rw_lock,            NULL);
    pthread_mutex_init(&thread_local_info_lock, NULL);
    pthread_key_create(&thread_local_info_key, (void(*)(void*))thread_local_info_fini);
    pthread_mutex_init(&access_profile_flush_mutex,        NULL);
    pthread_mutex_init(&access_profile_flush_signal_mutex, NULL);
    pthread_rwlock_init(&region_info_stack_rw_lock, NULL);

    pthread_mutex_lock(&access_profile_flush_signal_mutex);

    region_info_stack = parallel_region_stack_make();

    region_info_start_profile_thread();
}

void region_info_fini() {
    region_info_stop_profile_thread();

    pthread_rwlock_destroy(&region_info_stack_rw_lock);
    pthread_key_delete(thread_local_info_key);
    pthread_mutex_destroy(&thread_local_info_lock);
    pthread_rwlock_destroy(&at_rw_lock);
    pthread_rwlock_destroy(&chunks_end_rw_lock);
}

void region_info_inst_alloc(void *ptr, size_t size, uint64_t id) {
    thread_local_info *info;
    void              *my_chunks_end;

    CE_RLOCK(); {
        my_chunks_end = chunks_end;
    } CE_UNLOCK();

    if (ptr + size > my_chunks_end) {
        CE_WLOCK(); { 
            chunks_end = ptr + size;
        } CE_UNLOCK();
    }
  
    AT_WLOCK(); {
        tree_insert(alloc_tree, ptr, id);
    } AT_UNLOCK();

}

void region_info_inst_realloc(void *old_ptr, void *ptr, size_t size, uint64_t id) {
    tree_it(ptr_t, uint64_t)  it;
    void                     *my_chunks_end;

    if (old_ptr == NULL) {
        region_info_inst_alloc(ptr, size, id);
    } else {
        AT_WLOCK(); {
            it = tree_lookup(alloc_tree, old_ptr); 
            if (!tree_it_good(it)) {
                AT_UNLOCK();
                return;
            }

            CE_RLOCK(); {
                my_chunks_end = chunks_end;
            } CE_UNLOCK();

            if (ptr + size > my_chunks_end) {
                CE_WLOCK(); {
                    chunks_end = ptr + size;
                } CE_UNLOCK();
            }

            if (old_ptr == ptr) {
                tree_it_val(it) = id;
            } else {
                tree_delete(alloc_tree, old_ptr);
                tree_insert(alloc_tree, ptr, id);
            }
        } AT_UNLOCK();
    }
}

void region_info_inst_dealloc(void *ptr) {
    tree_it(ptr_t, uint64_t)  it;
    int                       deleted;

    AT_WLOCK(); { 
        deleted = tree_delete(alloc_tree, ptr);
    } AT_UNLOCK();
}

void region_info_inst_parallel_enter(uint64_t id) {
    parallel_region_info info;

    info = parallel_region_info_get_or_make(id);

    RI_LOCK(info); {
        info->concurrency += 1;
        if (info->in_stack) {
            RI_UNLOCK(info);
            return;
        }
    } RI_UNLOCK(info);

    RS_WLOCK(); {
        parallel_region_stack_push(&region_info_stack, info);
        trigger_access_info_flush();
        RI_LOCK(info); {
            info->in_stack = 1;
        } RI_UNLOCK(info);
    } RS_UNLOCK();
}

void region_info_inst_parallel_exit(uint64_t id) {
    parallel_region_info info, top;

    info = parallel_region_info_get_or_make(id);

    RI_LOCK(info); {
        ASSERT(info->concurrency > 0 && "concurrency underflow");
        ASSERT(info->in_stack && "region was never put on stack");
        info->concurrency -= 1;

        if (info->concurrency > 0) {
            RI_UNLOCK(info);
            return;
        }
    } RI_UNLOCK(info);

    RS_WLOCK(); {
        top = parallel_region_stack_top(&region_info_stack);
        ASSERT(top != NULL && "trying to exit a region, but no region on stack");
        ASSERT(top == info && "region stack top mismatch");

        trigger_access_info_flush();
        parallel_region_stack_pop(&region_info_stack);
        
        RI_LOCK(info); {
            info->in_stack = 0;
        } RI_UNLOCK(info);
    } RS_UNLOCK();
}

/*
 * @incomplete
 */
void region_info_inst_parallel_count() {
    int num_threads;

    num_threads = omp_get_num_threads();
}

/******************************************************************************
 * END:                                                  Runtime Implementation
 ******************************************************************************/
