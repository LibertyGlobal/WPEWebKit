#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <execinfo.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// memcheck will create report for each process
// in case of WPE we will have at least 4 instances: WPEWebProcess, WPENetworkProcess, OneMWBrowser, OneMWBrowser-debug
// fifth process (WPEDatabaseProcess) is created only when needed

#define MAX_TRACE 300000
#define MAX_CALLSTACK 16
#define TMP_CALLOC_BUFF (32 * 1024)

// adjust the size of searched allocations
// if the range will be to big, then WPE may be not operational
// (e.g. video may not play, due to memcheck overhead)
static unsigned memcheck_size_min = 16 * 1024;
static unsigned memcheck_size_max = 4 * 1024 * 1024;
static int startup_delay_in_sec = 30;
static unsigned int end_delay_in_sec = 30;
static unsigned int memcheck_duration = 30;

static char calloc_buffer[TMP_CALLOC_BUFF];
static int calloc_buffer_offset = 0;
static int calloc_buffer_nb = 0;
static unsigned int memory_trace_enabled = 0;
static unsigned int memory_trace_free_enabled = 0;
static uint64_t my_malloc_cnt = 0;
static uint64_t malloc_total_size = 0;
static uint64_t malloc_total = 0;
static uint64_t my_free_cnt = 0;
static uint64_t free_total_size = 0;
static uint64_t free_total = 0;
static uint64_t my_calloc_cnt = 0;
static uint64_t my_realloc_cnt = 0;
static int collect_callstack = 0;
static struct timeval start_time;

typedef struct {
    void *address;
    size_t size;
    pthread_t tid;
    int call_stack_nb;
    void* call_stack[MAX_CALLSTACK + 1];
    struct timeval timestamp;
} memory_trace_t;

static memory_trace_t memory_trace[MAX_TRACE];
static pthread_mutex_t memory_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

void memcheck_control_start() {
    fprintf(stderr, "%s:%d pid: %d\n", __FUNCTION__, __LINE__, getpid());
    pthread_mutex_lock(&memory_lock);
    gettimeofday(&start_time, NULL);
    memory_trace_enabled = 1;
    memory_trace_free_enabled = 1;
    pthread_mutex_unlock(&memory_lock);
}

void memcheck_control_stop() {
    fprintf(stderr, "%s:%d pid: %d\n", __FUNCTION__, __LINE__, getpid());
    pthread_mutex_lock(&memory_lock);
    memory_trace_enabled = 0;
    memory_trace_free_enabled = 1;
    pthread_mutex_unlock(&memory_lock);
}

void memcheck_control_show() {
    int pid = getpid();
    fprintf(stderr, "%s:%d pid %d\n", __FUNCTION__, __LINE__, pid);

    char path[64] = {0};

    snprintf(path, sizeof(path), "/tmp/wpe/memcheck-info_%d", pid);
    FILE* file = fopen(path, "a");

    if (!file) {
        fprintf(stderr, "%s:%d Failed to open file: %s\n", __FUNCTION__, __LINE__, path);
        return;
    }

    struct timeval now;

    gettimeofday(&now, NULL);

    int cntr = 0;
    const uint64_t offset_ms = ((uint64_t)start_time.tv_sec * 1000) + start_time.tv_usec / 1000;
    const uint64_t now_ms = ((uint64_t)now.tv_sec * 1000) + now.tv_usec / 1000;

    pthread_mutex_lock(&memory_lock);

    for (int i = 0; i < MAX_TRACE; ++i) {
        if (memory_trace[i].address == NULL) continue;

        char tname[32] = {0};

        pthread_getname_np(memory_trace[i].tid, tname, sizeof(tname));
        uint64_t ts_ms =
            ((uint64_t)memory_trace[i].timestamp.tv_sec * 1000)
            + memory_trace[i].timestamp.tv_usec / 1000 - offset_ms;

        fprintf(file, "ts: %" PRIu64 " addr: %8p, size: %9zu, tid: %s",
                ts_ms, memory_trace[i].address, memory_trace[i].size, tname);

        cntr++;

        if(collect_callstack) {
            for (int j = 0; j < memory_trace[i].call_stack_nb; ++j) {
                fprintf(file, " %p", memory_trace[i].call_stack[j]);
            }
        }
        fprintf(file, "\n");
    }

    pthread_mutex_unlock(&memory_lock);

    fprintf(
            file,
            "ts: %" PRIu64
            "ms %6d"
            " malloc_total_size:%" PRIu64
            " malloc:%" PRIu64
            " calloc:%" PRIu64
            " realloc:%" PRIu64
            " free_total_size:%" PRIu64
            " free:%" PRIu64
            " total: %" PRId64
            " total_size: %" PRId64
            "\n",
            now_ms - offset_ms,
            cntr,
            malloc_total_size,
            my_malloc_cnt,
            my_calloc_cnt,
            my_realloc_cnt,
            free_total_size,
            my_free_cnt,
            (int64_t)malloc_total - (int64_t)free_total,
            (int64_t)malloc_total_size - (int64_t)free_total_size);

    fprintf(stderr, "%s%d ts: %" PRIu64 "ms stats: tmp calloc %d %d\n",
            __FUNCTION__, __LINE__,
            now_ms - offset_ms, calloc_buffer_offset, calloc_buffer_nb);
    fclose(file);
}

void memcheck_configuration() {
    const char* max_size = getenv("WPE_MEMCHECK_MAX_SIZE");
    if (max_size) {
        memcheck_size_max = strtol(max_size, NULL, 10);
    }

    const char* min_size = getenv("WPE_MEMCHECK_MIN_SIZE");
    if (min_size) {
        memcheck_size_min = strtol(min_size, NULL, 10);
    }

    const char* start_delay = getenv("WPE_MEMCHECK_STARTUP_DELAY");
    if (start_delay) {
        startup_delay_in_sec = strtol(start_delay, NULL, 10);
    }

    const char* end_delay = getenv("WPE_MEMCHECK_END_DELAY");
    if (end_delay) {
        end_delay_in_sec = strtol(end_delay, NULL, 10);
    }

    const char* duration = getenv("WPE_MEMCHECK_DURATION");
    if (duration) {
        memcheck_duration = strtol(duration, NULL, 10);
    }
    const char *callstack = getenv("WPE_MEMCHECK_CALLSTACK");
    if(callstack) {
        collect_callstack = !!atoi(callstack);
    }
}

void* memcheck_control(void* not_used) {
    (void)not_used;
    memcheck_configuration();

    fprintf(stderr, "%s:%d pid: %d will start in: %d sec, will last: %d sec, allocation range: [%d B, %d B]\n",
            __FUNCTION__, __LINE__,
            getpid(), startup_delay_in_sec, memcheck_duration, memcheck_size_min, memcheck_size_max);


    struct timeval timestamp;
    gettimeofday(&timestamp, NULL);

    for(;;)
    {
        sleep(startup_delay_in_sec);
        struct timeval now;
        gettimeofday(&now, NULL);
        if(startup_delay_in_sec <= now.tv_sec - timestamp.tv_sec) break;
    }


    sleep(startup_delay_in_sec);

    memcheck_control_start();

    for(;;)
    {
        sleep(memcheck_duration);
        memcheck_control_show();
    }

    memcheck_control_stop();

    return NULL;
}

bool create_memcheck_thread() {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        fprintf(stderr, "ERROR memcheck: setting detach state failed\n");
        return false;
    }
    if (pthread_create(&thread, &attr, memcheck_control, NULL)) {
        fprintf(stderr, "ERROR memcheck: thread was not created\n");
        return false;
    }

    return true;
}

void* malloc(size_t size) {
    static void* (*libc_malloc)(size_t) = NULL;
    void * result = NULL;

    pthread_mutex_lock(&memory_lock);
    if (!libc_malloc) {
        libc_malloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc");

        // start memcheck control thread
        if (!create_memcheck_thread()) goto done;
    }

    result = libc_malloc(size);
    malloc_total_size += size;
    ++malloc_total;

    if (!memory_trace_enabled) goto done;

    if (result && (size >= memcheck_size_min && size <= memcheck_size_max)) {

        struct timeval timestamp;

        gettimeofday(&timestamp, NULL);

        for (int i = 0; i < MAX_TRACE; ++i) {
            if (memory_trace[i].address == NULL) {
                memory_trace[i].address = result;
                memory_trace[i].size = size;
                memory_trace[i].tid = pthread_self();
                memory_trace[i].timestamp = timestamp;
                if (collect_callstack) {
                    /*
                    when backtrace size is equal to 1 then most probably the function that trigger our malloc wrapper is
                    compiled without appropriate flags (-rdynamic, -fasynchronous-unwind-tables, -funwind-tables) that allow
                    backtrace() to find previous stackframes
                    the same behavior applies to calloc() and realloc() wrappers
                    */
                    memory_trace[i].call_stack_nb = backtrace(memory_trace[i].call_stack, MAX_CALLSTACK);
                }
                my_malloc_cnt++;
                goto done;
            }
        }
    }
done:
    pthread_mutex_unlock(&memory_lock);
    return result;
}

static void* tmp_calloc(size_t nmemb, size_t size) {
    void* p = NULL;
    size_t s = nmemb * size;
    size_t s_alloc;

    // align to 4 bytes
    s_alloc = (s + 3) & 0xfffffffc;
    if ((calloc_buffer_offset + sizeof(s) + s_alloc) <= TMP_CALLOC_BUFF) {
        memcpy( & calloc_buffer[calloc_buffer_offset], &s, sizeof(s)); // store calloc size
        p = &calloc_buffer[calloc_buffer_offset + sizeof(s)];
        memset(p, 0, s);
        calloc_buffer_offset += sizeof(s) + s_alloc;
        calloc_buffer_nb++;
    }
    return p;
}

static bool is_tmp_calloc(void* p) {
    if (((char*) p >= calloc_buffer) && ((char*) p < (calloc_buffer + TMP_CALLOC_BUFF))) {
        return true;
    }
    return false;
}

static unsigned int tmp_calloc_size(void* p) {
    unsigned int s = 0;
    if (is_tmp_calloc(p)) {
        memcpy(&s, (char*) p - sizeof(s), sizeof(s));
    }
    return s;
}

void* calloc(size_t nmemb, size_t size) {
    static void* (*libc_calloc)(size_t, size_t) = NULL;
    void* result;
    // need to prevent gcc compiler optimizations
    static volatile int calloc_init = 0;
    int i;

    pthread_mutex_lock(&memory_lock);
    if (calloc_init) {
        result = tmp_calloc(nmemb, size); // if calloc is called from dlsym
        pthread_mutex_unlock(&memory_lock);
        return result;
    }

    if (!libc_calloc) {
        calloc_init = 1;
        libc_calloc = (void* (*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
        calloc_init = 0;
    }

    result = libc_calloc(nmemb, size);

    if (!memory_trace_enabled) {
        pthread_mutex_unlock(&memory_lock);
        return result;
    }

    if (result && (size >= memcheck_size_min && size <= memcheck_size_max)) {
        struct timeval timestamp;

        gettimeofday(&timestamp, NULL);

        for (i = 0; i < MAX_TRACE; ++i) {
            if (memory_trace[i].address == NULL) {
                memory_trace[i].address = result;
                memory_trace[i].size = size;
                memory_trace[i].tid = pthread_self();
                memory_trace[i].timestamp = timestamp;
                if (collect_callstack) {
                    memory_trace[i].call_stack_nb = backtrace(memory_trace[i].call_stack, MAX_CALLSTACK);
                }
                my_calloc_cnt++;
                break;
            }
        }
    }

    pthread_mutex_unlock(&memory_lock);
    return result;
}

void* realloc(void* ptr, size_t size) {
    static void* (*libc_realloc)(void* , size_t) = NULL;
    void * result = NULL;

    pthread_mutex_lock(&memory_lock);
    if (!libc_realloc) {
        libc_realloc = (void* (*)(void* , size_t)) dlsym(RTLD_NEXT, "realloc");
    }

    if (is_tmp_calloc(ptr)) {
        result = malloc(size);
        if (result) {
            unsigned s1 = tmp_calloc_size(ptr);
            memcpy(result, ptr, s1 < size ? s1 : size);
        }
    } else {
        result = libc_realloc(ptr, size);
        if (memory_trace_free_enabled) {
            for (int i = 0; i < MAX_TRACE; i++) {
                if (memory_trace[i].address == ptr) {
                    memory_trace[i].address = NULL;
                    my_free_cnt++;
                    break;
                }
            }
        }
    }

    if (!memory_trace_enabled) goto done;

    if (result && (size >= memcheck_size_min && size <= memcheck_size_max)) {
        struct timeval timestamp;

        gettimeofday(&timestamp, NULL);

        for (int i = 0; i < MAX_TRACE; i++) {
            if (memory_trace[i].address == NULL) {
                memory_trace[i].address = result;
                memory_trace[i].size = size;
                memory_trace[i].tid = pthread_self();
                memory_trace[i].timestamp = timestamp;
                if (collect_callstack) {
                    memory_trace[i].call_stack_nb = backtrace(memory_trace[i].call_stack, MAX_CALLSTACK);
                }
                my_realloc_cnt++;
                goto done;
            }
        }
    }
done:
    pthread_mutex_unlock(&memory_lock);
    return result;
}

void free(void* p) {
    static void (*libc_free)(void*) = NULL;
    pthread_mutex_lock(&memory_lock);
    if (!libc_free) {
        libc_free = (void (*)(void*)) dlsym(RTLD_NEXT, "free");
    }

    if(!p) goto exit;
    if (is_tmp_calloc(p)) goto exit;

    if (!memory_trace_free_enabled) goto done;

    for (int i = 0; i < MAX_TRACE; i++) {
        if (memory_trace[i].address == p) {
            free_total_size += memory_trace[i].size;
            memory_trace[i].address = NULL;
            my_free_cnt++;
            goto done;
        }
    }
done:
    libc_free(p);
    ++free_total;
exit:
    pthread_mutex_unlock(&memory_lock);
}
