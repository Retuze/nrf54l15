/* libc++ external threading hooks backed by RT-Thread. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <picotls.h>
#include <rthw.h>
#include <rtthread.h>

typedef void *         __libcpp_mutex_t;
typedef void *         __libcpp_recursive_mutex_t;
typedef void *         __libcpp_condvar_t;
typedef int            __libcpp_exec_once_flag;
typedef void *         __libcpp_thread_t;
typedef unsigned long  __libcpp_thread_id;
typedef unsigned long  __libcpp_tls_key;

#define CXX_TLS_MAX_KEYS 32u

typedef struct {
    rt_sem_t sem;
    rt_mutex_t lock;
    rt_int32_t waiters;
    rt_int32_t pending_wakeups;
} cxx_condvar_t;

typedef struct {
    rt_thread_t tid;
    rt_sem_t done;
    void *(*fn)(void *);
    void *arg;
    void *ret;
    rt_bool_t done_flag;
    rt_bool_t detached;
} cxx_thread_t;

typedef struct {
    void *tls_alloc;
    void *tls_block;
    void *values[CXX_TLS_MAX_KEYS];
    void (*prev_cleanup)(struct rt_thread *tid);
} cxx_thread_ctx_t;

#ifndef LIBCPP_THREAD_STACK_SIZE
#define LIBCPP_THREAD_STACK_SIZE 4096
#endif

#ifndef LIBCPP_THREAD_PRIORITY
#define LIBCPP_THREAD_PRIORITY 12
#endif

#ifndef LIBCPP_THREAD_TICK
#define LIBCPP_THREAD_TICK 10
#endif

static void (*g_tls_dtors[CXX_TLS_MAX_KEYS])(void *);
static __libcpp_tls_key g_tls_next_key = 1u;
static rt_bool_t g_cxx_tls_runtime_ready;

static uintptr_t cxx_tls_align_up(uintptr_t value, size_t align)
{
    size_t mask;

    if (align <= 1u) {
        return value;
    }

    mask = align - 1u;
    return (value + mask) & ~(uintptr_t)mask;
}

static cxx_thread_ctx_t *cxx_tls_ctx_from_thread(rt_thread_t thread)
{
    if (thread == RT_NULL || thread->user_data == 0u) {
        return RT_NULL;
    }

    return (cxx_thread_ctx_t *)(uintptr_t)thread->user_data;
}

static cxx_thread_ctx_t *cxx_tls_current_ctx(void)
{
    return cxx_tls_ctx_from_thread(rt_thread_self());
}

static void cxx_tls_activate_thread(rt_thread_t thread)
{
    cxx_thread_ctx_t *ctx = cxx_tls_ctx_from_thread(thread);

    if (ctx == RT_NULL) {
        return;
    }

    _set_tls(ctx->tls_block);
}

static cxx_thread_ctx_t *cxx_tls_allocate_ctx(rt_thread_t thread)
{
    cxx_thread_ctx_t *ctx;
    size_t tls_size;
    size_t tls_align;
    size_t alloc_size;
    void *tls_alloc;
    uintptr_t tls_block;

    ctx = (cxx_thread_ctx_t *)rt_calloc(1, sizeof(cxx_thread_ctx_t));
    if (ctx == RT_NULL) {
        return RT_NULL;
    }

    tls_size = _tls_size();
    tls_align = _tls_align();
    if (tls_align < sizeof(void *)) {
        tls_align = sizeof(void *);
    }

    alloc_size = tls_size + tls_align;
    if (alloc_size == 0u) {
        alloc_size = tls_align;
    }

    tls_alloc = rt_malloc(alloc_size);
    if (tls_alloc == RT_NULL) {
        rt_free(ctx);
        return RT_NULL;
    }

    tls_block = cxx_tls_align_up((uintptr_t)tls_alloc, tls_align);
    ctx->tls_alloc = tls_alloc;
    ctx->tls_block = (void *)tls_block;
    ctx->prev_cleanup = thread->cleanup;

    _init_tls(ctx->tls_block);
    return ctx;
}

static cxx_thread_ctx_t *cxx_tls_ensure_thread(rt_thread_t thread)
{
    cxx_thread_ctx_t *ctx = cxx_tls_ctx_from_thread(thread);

    if (ctx != RT_NULL) {
        return ctx;
    }

    ctx = cxx_tls_allocate_ctx(thread);
    if (ctx == RT_NULL) {
        return RT_NULL;
    }

    thread->user_data = (rt_ubase_t)(uintptr_t)ctx;
    thread->cleanup = RT_NULL;
    return ctx;
}

static void cxx_tls_thread_cleanup(struct rt_thread *thread)
{
    __libcpp_tls_key key;
    cxx_thread_ctx_t *ctx = cxx_tls_ctx_from_thread(thread);

    if (ctx == RT_NULL) {
        return;
    }

    if (ctx->prev_cleanup != RT_NULL) {
        ctx->prev_cleanup(thread);
    }

    for (key = 1u; key < g_tls_next_key; key++) {
        void (*dtor)(void *) = g_tls_dtors[key];
        void *value = ctx->values[key];

        if (dtor != RT_NULL && value != RT_NULL) {
            ctx->values[key] = RT_NULL;
            dtor(value);
        }
    }

    thread->cleanup = ctx->prev_cleanup;
    thread->user_data = 0u;
    rt_free(ctx->tls_alloc);
    rt_free(ctx);
}

static void cxx_thread_inited_hook(rt_thread_t thread)
{
    cxx_thread_ctx_t *ctx = cxx_tls_ensure_thread(thread);

    if (ctx == RT_NULL) {
        return;
    }

    thread->cleanup = cxx_tls_thread_cleanup;
}

static void cxx_scheduler_hook(rt_thread_t from, rt_thread_t to)
{
    (void)from;
    cxx_tls_activate_thread(to);
}

void libcpp_threads_init(void)
{
    if (g_cxx_tls_runtime_ready) {
        return;
    }

    rt_thread_inited_sethook(cxx_thread_inited_hook);
    rt_scheduler_sethook(cxx_scheduler_hook);
    g_cxx_tls_runtime_ready = RT_TRUE;
}

static rt_err_t cxx_mutex_ensure(__libcpp_mutex_t *m, const char *name)
{
    rt_mutex_t mutex = (rt_mutex_t)*m;

    if (mutex == RT_NULL) {
        rt_base_t level;
        rt_mutex_t new_mutex = rt_mutex_create(name, RT_IPC_FLAG_PRIO);

        if (new_mutex == RT_NULL) {
            return -RT_ERROR;
        }

        level = rt_hw_interrupt_disable();
        if (*m == RT_NULL) {
            *m = (void *)new_mutex;
            mutex = new_mutex;
            new_mutex = RT_NULL;
        } else {
            mutex = (rt_mutex_t)*m;
        }
        rt_hw_interrupt_enable(level);

        if (new_mutex != RT_NULL) {
            (void)rt_mutex_delete(new_mutex);
        }
    }

    return (mutex == RT_NULL) ? -RT_ERROR : RT_EOK;
}

static rt_tick_t cxx_timespec_to_ticks(const struct timespec *ts)
{
    long long now_ns;
    long long target_ns;
    long long delta_ns;
    long long ms;

    if (ts == NULL) {
        return RT_WAITING_FOREVER;
    }

    now_ns = (long long)rt_tick_get_millisecond() * 1000000LL;
    target_ns = (long long)ts->tv_sec * 1000000000LL + (long long)ts->tv_nsec;
    delta_ns = target_ns - now_ns;
    if (delta_ns <= 0) {
        return 0;
    }

    ms = (delta_ns + 999999LL) / 1000000LL;
    if (ms <= 0) {
        return 1;
    }

    return rt_tick_from_millisecond((rt_int32_t)ms);
}

int __libcpp_mutex_lock(__libcpp_mutex_t *m)
{
    if (cxx_mutex_ensure(m, "cxxm") != RT_EOK) {
        return -1;
    }
    return (rt_mutex_take((rt_mutex_t)*m, RT_WAITING_FOREVER) == RT_EOK) ? 0 : -1;
}

bool __libcpp_mutex_trylock(__libcpp_mutex_t *m)
{
    if (cxx_mutex_ensure(m, "cxxm") != RT_EOK) {
        return false;
    }
    return rt_mutex_take((rt_mutex_t)*m, 0) == RT_EOK;
}

int __libcpp_mutex_unlock(__libcpp_mutex_t *m)
{
    if (*m == RT_NULL) {
        return -1;
    }
    return (rt_mutex_release((rt_mutex_t)*m) == RT_EOK) ? 0 : -1;
}

int __libcpp_mutex_destroy(__libcpp_mutex_t *m)
{
    if (*m != RT_NULL) {
        (void)rt_mutex_delete((rt_mutex_t)*m);
        *m = RT_NULL;
    }
    return 0;
}

int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t *m)
{
    return (cxx_mutex_ensure((__libcpp_mutex_t *)m, "cxxr") == RT_EOK) ? 0 : -1;
}

int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t *m)
{
    return __libcpp_mutex_lock((__libcpp_mutex_t *)m);
}

bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t *m)
{
    return __libcpp_mutex_trylock((__libcpp_mutex_t *)m);
}

int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t *m)
{
    return __libcpp_mutex_unlock((__libcpp_mutex_t *)m);
}

int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t *m)
{
    return __libcpp_mutex_destroy((__libcpp_mutex_t *)m);
}

static cxx_condvar_t *cxx_condvar_ensure(__libcpp_condvar_t *c)
{
    cxx_condvar_t *cv = (cxx_condvar_t *)*c;

    if (cv != RT_NULL) {
        return cv;
    }

    cv = (cxx_condvar_t *)rt_malloc(sizeof(cxx_condvar_t));
    if (cv == RT_NULL) {
        return RT_NULL;
    }

    cv->sem = rt_sem_create("cxxc", 0, RT_IPC_FLAG_PRIO);
    cv->lock = rt_mutex_create("cxxl", RT_IPC_FLAG_PRIO);
    cv->waiters = 0;
    cv->pending_wakeups = 0;
    if (cv->sem == RT_NULL || cv->lock == RT_NULL) {
        if (cv->sem != RT_NULL) {
            (void)rt_sem_delete(cv->sem);
        }
        if (cv->lock != RT_NULL) {
            (void)rt_mutex_delete(cv->lock);
        }
        rt_free(cv);
        return RT_NULL;
    }

    {
        rt_base_t level;
        cxx_condvar_t *installed;

        level = rt_hw_interrupt_disable();
        installed = (cxx_condvar_t *)*c;
        if (installed == RT_NULL) {
            *c = (void *)cv;
            installed = cv;
            cv = RT_NULL;
        }
        rt_hw_interrupt_enable(level);

        if (cv != RT_NULL) {
            (void)rt_sem_delete(cv->sem);
            (void)rt_mutex_delete(cv->lock);
            rt_free(cv);
        }

        return installed;
    }
}

int __libcpp_condvar_signal(__libcpp_condvar_t *c)
{
    cxx_condvar_t *cv = cxx_condvar_ensure(c);

    if (cv == RT_NULL) {
        return -1;
    }

    (void)rt_mutex_take(cv->lock, RT_WAITING_FOREVER);
    if (cv->waiters > cv->pending_wakeups) {
        cv->pending_wakeups++;
        (void)rt_sem_release(cv->sem);
    }
    (void)rt_mutex_release(cv->lock);
    return 0;
}

int __libcpp_condvar_broadcast(__libcpp_condvar_t *c)
{
    cxx_condvar_t *cv = cxx_condvar_ensure(c);
    rt_int32_t n;
    rt_int32_t i;

    if (cv == RT_NULL) {
        return -1;
    }

    (void)rt_mutex_take(cv->lock, RT_WAITING_FOREVER);
    n = cv->waiters - cv->pending_wakeups;
    if (n > 0) {
        cv->pending_wakeups += n;
    }
    for (i = 0; i < n; i++) {
        (void)rt_sem_release(cv->sem);
    }
    (void)rt_mutex_release(cv->lock);
    return 0;
}

static int cxx_condvar_wait_impl(__libcpp_condvar_t *c, __libcpp_mutex_t *m, rt_tick_t timeout)
{
    cxx_condvar_t *cv = cxx_condvar_ensure(c);
    rt_err_t rc;

    if (cv == RT_NULL || *m == RT_NULL) {
        return -1;
    }

    (void)rt_mutex_take(cv->lock, RT_WAITING_FOREVER);
    cv->waiters++;
    (void)rt_mutex_release(cv->lock);

    (void)rt_mutex_release((rt_mutex_t)*m);
    rc = rt_sem_take(cv->sem, timeout);

    (void)rt_mutex_take(cv->lock, RT_WAITING_FOREVER);
    if (cv->waiters > 0) {
        cv->waiters--;
    }

    if (rc == RT_EOK) {
        if (cv->pending_wakeups > 0) {
            cv->pending_wakeups--;
        }
        (void)rt_mutex_release(cv->lock);
        (void)rt_mutex_take((rt_mutex_t)*m, RT_WAITING_FOREVER);
        return 0;
    }

    if (cv->pending_wakeups > 0) {
        rt_err_t drain_rc;

        cv->pending_wakeups--;
        (void)rt_mutex_release(cv->lock);
        drain_rc = rt_sem_take(cv->sem, 0);
        (void)drain_rc;
        (void)rt_mutex_take((rt_mutex_t)*m, RT_WAITING_FOREVER);
        return 0;
    }

    (void)rt_mutex_release(cv->lock);
    (void)rt_mutex_take((rt_mutex_t)*m, RT_WAITING_FOREVER);
    return ETIMEDOUT;
}

int __libcpp_condvar_wait(__libcpp_condvar_t *c, __libcpp_mutex_t *m)
{
    return cxx_condvar_wait_impl(c, m, RT_WAITING_FOREVER);
}

int __libcpp_condvar_timedwait(__libcpp_condvar_t *c, __libcpp_mutex_t *m, struct timespec *ts)
{
    return cxx_condvar_wait_impl(c, m, cxx_timespec_to_ticks(ts));
}

int __libcpp_condvar_destroy(__libcpp_condvar_t *c)
{
    cxx_condvar_t *cv = (cxx_condvar_t *)*c;
    if (cv != RT_NULL) {
        (void)rt_sem_delete(cv->sem);
        (void)rt_mutex_delete(cv->lock);
        rt_free(cv);
        *c = RT_NULL;
    }
    return 0;
}

int __libcpp_execute_once(__libcpp_exec_once_flag *flag, void (*init)(void))
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (*flag == 0) {
        *flag = 1;
        rt_hw_interrupt_enable(level);
        init();
        return 0;
    }
    rt_hw_interrupt_enable(level);
    return 0;
}

bool __libcpp_thread_id_equal(__libcpp_thread_id a, __libcpp_thread_id b) { return a == b; }
bool __libcpp_thread_id_less(__libcpp_thread_id a, __libcpp_thread_id b) { return a < b; }

bool __libcpp_thread_isnull(const __libcpp_thread_t *t) { return *t == RT_NULL; }

static void cxx_thread_entry(void *parameter)
{
    cxx_thread_t *th = (cxx_thread_t *)parameter;
    th->ret = th->fn(th->arg);
    th->done_flag = RT_TRUE;
    (void)rt_sem_release(th->done);

    if (th->detached) {
        (void)rt_sem_delete(th->done);
        rt_free(th);
    }
}

int __libcpp_thread_create(__libcpp_thread_t *t, void *(*fn)(void *), void *arg)
{
    cxx_thread_t *th = (cxx_thread_t *)rt_malloc(sizeof(cxx_thread_t));
    if (th == RT_NULL) {
        return -1;
    }

    th->done = rt_sem_create("cxxd", 0, RT_IPC_FLAG_PRIO);
    th->fn = fn;
    th->arg = arg;
    th->ret = RT_NULL;
    th->done_flag = RT_FALSE;
    th->detached = RT_FALSE;
    if (th->done == RT_NULL) {
        rt_free(th);
        return -1;
    }

    th->tid = rt_thread_create("cxxth", cxx_thread_entry, th,
                               LIBCPP_THREAD_STACK_SIZE,
                               LIBCPP_THREAD_PRIORITY,
                               LIBCPP_THREAD_TICK);
    if (th->tid == RT_NULL) {
        (void)rt_sem_delete(th->done);
        rt_free(th);
        return -1;
    }

    if (rt_thread_startup(th->tid) != RT_EOK) {
        (void)rt_sem_delete(th->done);
        rt_free(th);
        return -1;
    }

    *t = (void *)th;
    return 0;
}

__libcpp_thread_id __libcpp_thread_get_current_id(void)
{
    return (__libcpp_thread_id)(uintptr_t)rt_thread_self();
}

__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t *t)
{
    const cxx_thread_t *th = (const cxx_thread_t *)*t;
    if (th == RT_NULL) {
        return 0;
    }
    return (__libcpp_thread_id)(uintptr_t)th->tid;
}

int __libcpp_thread_join(__libcpp_thread_t *t)
{
    cxx_thread_t *th = (cxx_thread_t *)*t;
    if (th == RT_NULL) {
        return -1;
    }
    (void)rt_sem_take(th->done, RT_WAITING_FOREVER);
    (void)rt_sem_delete(th->done);
    rt_free(th);
    *t = RT_NULL;
    return 0;
}

int __libcpp_thread_detach(__libcpp_thread_t *t)
{
    cxx_thread_t *th = (cxx_thread_t *)*t;
    if (th == RT_NULL) {
        return -1;
    }
    th->detached = RT_TRUE;
    if (th->done_flag) {
        (void)rt_sem_delete(th->done);
        rt_free(th);
    }
    *t = RT_NULL;
    return 0;
}

void __libcpp_thread_yield(void)
{
    rt_thread_yield();
}

void __libcpp_thread_sleep_for_ns(long long ns)
{
    long long ms;
    if (ns <= 0) {
        rt_thread_yield();
        return;
    }
    ms = (ns + 999999LL) / 1000000LL;
    if (ms <= 0) {
        ms = 1;
    }
    rt_thread_mdelay((rt_int32_t)ms);
}

int __libcpp_tls_create(__libcpp_tls_key *key, void (*dtor)(void *))
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (g_tls_next_key >= CXX_TLS_MAX_KEYS) {
        rt_hw_interrupt_enable(level);
        return -1;
    }
    *key = g_tls_next_key++;
    g_tls_dtors[*key] = dtor;
    rt_hw_interrupt_enable(level);
    return 0;
}

void *__libcpp_tls_get(__libcpp_tls_key key)
{
    cxx_thread_ctx_t *ctx;

    if (key == 0u || key >= CXX_TLS_MAX_KEYS) {
        return RT_NULL;
    }

    ctx = cxx_tls_current_ctx();
    if (ctx == RT_NULL) {
        return RT_NULL;
    }

    return ctx->values[key];
}

int __libcpp_tls_set(__libcpp_tls_key key, void *v)
{
    cxx_thread_ctx_t *ctx;

    if (key == 0u || key >= CXX_TLS_MAX_KEYS) {
        return -1;
    }

    ctx = cxx_tls_current_ctx();
    if (ctx == RT_NULL) {
        (void)v;
        return -1;
    }

    ctx->values[key] = v;
    return 0;
}
