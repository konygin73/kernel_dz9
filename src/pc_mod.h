#ifndef PC_MOD_H
#define PC_MOD_H

#ifndef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/timer.h>

#define MP_OK        0           /* операция успешна             */
#define MP_INVALID  (-EINVAL)    /* неверный параметр            */
#define MP_NOMEM    (-ENOMEM)    /* недостаточно памяти          */
#define MP_BUSY     (-EBUSY)     /* операция недоступна сейчас   */
#define MP_FULL     (-ENOBUFS)   /* очередь переполнена          */

#define MSG_QUEUE_MAX   16      /*Максимальный размер очереди сообщений                                 */
#define MSG_TEXT_MAX    128     /*Максимальная длина текста одного сообщения байт (включая \0)          */
#define MIN_MIN_NR      1       /*Минимальный резерв объектов в mempool (используется при alloc_type=1) */
#define MAX_MIN_NR      64      /*Минимальный резерв объектов в mempool (используется при alloc_type=1) */
#define MIN_INTERVAL_MS 100     /*интервал таймера min                                                  */
#define MAX_INTERVAL_MS 60000   /*интервал таймера max                                                  */

#define ALLOCATOR T_KMEM_CACHE  /*значение по умолчанию */
#define POOL_MIN_NR 8
#define TIMER_INTERVAL_MS 10000

enum alloc_etype {
    T_KMEM_CACHE = 0,
    T_MEMPOOL
};

struct msg {
    char          text[MSG_TEXT_MAX];
    ktime_t       enqueue_time;/* момент постановки в очередь */
    unsigned int  seq;/* порядковый номер сообщения */
};

struct msg_queue {
    struct msg   *slots[MSG_QUEUE_MAX]; /* слоты очереди (кольцевой буфер) */
    unsigned int  head;                 /* индекс чтения                   */
    unsigned int  tail;                 /* индекс записи                   */
    unsigned int  count;                /* текущее число элементов         */
    spinlock_t    lock;                 /* защита head/tail/count          */
};

struct msgpool_ctx {
    enum alloc_etype  alloc_type;    /*Тип аллокатора: 0 — kmem_cache, 1 — mempool, допустимо только при пустой очереди */
    unsigned int      pool_min_nr;   /*Минимальный резерв объектов в mempool (при alloc_type=1) */
    unsigned int      interval_ms;   /*Интервал таймера ms >= 100 и <= 60000 */

    struct kmem_cache *msg_cache;    /* slab-кеш объектов struct msg       */
    mempool_t         *msg_pool;     /* пул поверх msg_cache (alloc_type=1) */

    struct msg_queue  queue;

    struct timer_list consumer_timer;

    /* Статистика */
    atomic_t  sent_total;            /* всего поставлено в очередь         */
    atomic_t  consumed_total;        /* всего обработано таймером          */
    atomic_t  flushed_total;         /* всего сброшено через flush         */
    atomic_t  dropped_total;         /* отброшено (очередь была полна)     */

    /* Последнее обработанное сообщение (для параметра inbox) */
    char       last_msg[MSG_TEXT_MAX];
    spinlock_t last_msg_lock;

    unsigned int seq_counter;        /* монотонный счётчик сообщений       */
};

extern int g_is_init;
extern int g_alloc_type;
extern int g_interval_ms;
extern struct msgpool_ctx g_msgpool_ctx;

struct msg* alloc_msg(struct msgpool_ctx *msgpool);
void free_msg(struct msgpool_ctx *msgpool, struct msg *message);
int msgpool_ctx_push(const char *val);
int msgpool_ctx_get(char *buf, size_t buf_size);
void msgpool_ctx_flush(void);
int msgpool_ctx_init(void);

#endif /* PC_MOD_H */
