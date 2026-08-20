#include "pc_mod.h"

static const enum alloc_etype ALLOCATOR = T_KMEM_CACHE; //значение по умолчанию
static const uint POOL_MIN_NR = 8;
static const unsigned long TIMER_INTERVAL_MS = 10000;

struct msgpool_ctx g_msgpool_ctx;

int msgpool_ctx_init(void) {
  g_msgpool_ctx.alloc_type = ALLOCATOR;
  g_msgpool_ctx.pool_min_nr = POOL_MIN_NR;
  g_msgpool_ctx.interval_ms = TIMER_INTERVAL_MS;
  g_msgpool_ctx.msg_cache = NULL;
  g_msgpool_ctx.msg_pool = NULL;

  g_msgpool_ctx.queue.head = 0;
  g_msgpool_ctx.queue.tail = 0;
  g_msgpool_ctx.queue.count = 0;
  spin_lock_init(&g_msgpool_ctx.queue.lock);

  atomic_set(&g_msgpool_ctx.sent_total, 0);
  atomic_set(&g_msgpool_ctx.consumed_total, 0);
  atomic_set(&g_msgpool_ctx.flushed_total, 0);
  atomic_set(&g_msgpool_ctx.dropped_total, 0);

  g_msgpool_ctx.last_msg[0] = 0;
  spin_lock_init(&g_msgpool_ctx.last_msg_lock);
  g_msgpool_ctx.seq_counter = 0;

  g_msgpool_ctx.msg_cache = kmem_cache_create(
      "msgpool_cache", sizeof(struct msg), 0, SLAB_HWCACHE_ALIGN, NULL);
  if (!g_msgpool_ctx.msg_cache) {
    return MP_NOMEM;
  }
  if (g_msgpool_ctx.alloc_type == T_MEMPOOL) {
    g_msgpool_ctx.msg_pool = mempool_create_slab_pool(g_msgpool_ctx.pool_min_nr,
                                                      g_msgpool_ctx.msg_cache);
    if (!g_msgpool_ctx.msg_pool) {
      kmem_cache_destroy(g_msgpool_ctx.msg_cache);
      g_msgpool_ctx.msg_cache = NULL;
      return MP_NOMEM;
    }
  }
  return g_msgpool_ctx.msg_cache != NULL ? MP_OK : MP_NOMEM;
}

int msgpool_ctx_push(const char *val) {
  unsigned long flags;
  struct msg_queue *queue = &g_msgpool_ctx.queue;

  struct msg *message = alloc_msg(&g_msgpool_ctx);
  if (message == NULL) {
    return MP_NOMEM;
  }

  spin_lock_irqsave(&queue->lock, flags);
  if (queue->count >= MSG_QUEUE_MAX) {
    atomic_inc(&g_msgpool_ctx.dropped_total);
    spin_unlock_irqrestore(&queue->lock, flags);
    free_msg(&g_msgpool_ctx, message);
    return MP_FULL;
  }

  message->seq = g_msgpool_ctx.seq_counter++;
  message->enqueue_time = ktime_get();
  strscpy(message->text, val, MSG_TEXT_MAX);

  queue->slots[queue->tail] = message;
  ++queue->count;
  queue->tail = (queue->tail + 1) % MSG_QUEUE_MAX;
  spin_unlock_irqrestore(&queue->lock, flags);

  atomic_inc(&g_msgpool_ctx.sent_total);

  return MP_OK;
}

int msgpool_ctx_get(char *buf, size_t buf_size) {
  unsigned long flags;
  struct msg_queue *queue = &g_msgpool_ctx.queue;

  spin_lock_irqsave(&queue->lock, flags);
  if (queue->count == 0) {
    spin_unlock_irqrestore(&queue->lock, flags);
    return MP_BUSY;
  }

  int seq = queue->head;
  struct msg *message = queue->slots[seq];
  queue->head = (seq + 1) % MSG_QUEUE_MAX;
  --queue->count;
  spin_unlock_irqrestore(&queue->lock, flags);

  s64 elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), message->enqueue_time));
  pr_info("msgpool: [%u] %s (queued %lld ns ago)\n", message->seq,
          message->text, elapsed_ns);

  spin_lock_irqsave(&g_msgpool_ctx.last_msg_lock, flags);
  strscpy(g_msgpool_ctx.last_msg, message->text,
          sizeof(g_msgpool_ctx.last_msg));
  spin_unlock_irqrestore(&g_msgpool_ctx.last_msg_lock, flags);

  strscpy(buf, message->text, buf_size);

  free_msg(&g_msgpool_ctx, message);
  atomic_inc(&g_msgpool_ctx.consumed_total);

  return MP_OK;
}

void msgpool_ctx_flush(void) {
  unsigned long flags;
  struct msg_queue *queue = &g_msgpool_ctx.queue;

  spin_lock_irqsave(&queue->lock, flags);
  while (queue->count > 0) {
    struct msg *message = queue->slots[queue->head];
    queue->head = (queue->head + 1) % MSG_QUEUE_MAX;
    --queue->count;
    free_msg(&g_msgpool_ctx, message);
    atomic_inc(&g_msgpool_ctx.flushed_total);
  }
  spin_unlock_irqrestore(&queue->lock, flags);
}