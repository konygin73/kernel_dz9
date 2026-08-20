
#include "pc_mod.h"
#include <linux/log2.h>

#define __STR(x) #x
#define STR(x) __STR(x)

int g_alloc_type = ALLOCATOR;
static int param_get_allocator_ops(char *buffer,
                                   const struct kernel_param *kp) {
  const char KMEM_CASHE[] = {"Alloc type KMEM_CASHE\n"};
  const char MEMPOOL[] = {"Alloc type MEMPOOL\n"};

  const char *alloc = KMEM_CASHE;

  if (g_msgpool_ctx.alloc_type == T_MEMPOOL) {
    alloc = MEMPOOL;
  }

  return scnprintf(buffer, PAGE_SIZE, "%s", alloc);
}
static int param_set_allocator_ops(const char *val,
                                   const struct kernel_param *kp) {
  unsigned long flags;
  int allocator;
  int ret;

  ret = kstrtouint(val, 0, &allocator);
  if (ret) {
    return ret;
  }

  if (allocator != 0 && allocator != 1) {
    pr_err("Invalid allocator %u.\n", allocator);
    return -EINVAL;
  }

  g_alloc_type = allocator;
  if (g_is_init == 0) {
    return MP_OK;  
  }

  struct msg_queue *queue = &g_msgpool_ctx.queue;
  spin_lock_irqsave(&queue->lock, flags);
  if (queue->count != 0) {
    pr_info("Аллокатор не меняется для не пустой очереди\n");
  } else {
    g_msgpool_ctx.alloc_type = allocator;
    pr_info("Аллокатор: %s\n", allocator == 0 ? "KMEM_CACHE" : "MEMPOOL");
  }
  spin_unlock_irqrestore(&queue->lock, flags);

  return MP_OK;
}
static const struct kernel_param_ops type_allocator_ops = {
    .set = param_set_allocator_ops,
    .get = param_get_allocator_ops,
};
module_param_cb(alloc_type, &type_allocator_ops, NULL, 0644);
MODULE_PARM_DESC(alloc_type, "alloc_type 0 KMEM_CASHE или 1 MEMPOOL");

static int param_set_send_ops(const char *val, const struct kernel_param *kp) {
  size_t len;
  char msg[MSG_TEXT_MAX];

  if (!val) {
    return -EINVAL;
  }

  len = strlen(val);
  if (len > MSG_TEXT_MAX) {
    pr_info("The line of text is cut off\n");
  }

  ssize_t ret = strscpy(msg, val, MSG_TEXT_MAX);
  if (ret < 0) {
    return -ENOSPC;
  }

  if (ret > 0 && msg[ret - 1] == '\n') {
    msg[ret - 1] = '\0';
  }

  int ret_push = msgpool_ctx_push(msg);
  if (ret_push != MP_OK) {
    pr_info("Send line ret %d\n", ret_push);
  }

  return MP_OK;
}
static const struct kernel_param_ops fifo_line_ops = {
    .set = param_set_send_ops,
};
module_param_cb(send, &fifo_line_ops, NULL, 0200);
MODULE_PARM_DESC(send, "send text < " STR(MSG_TEXT_MAX));

static int param_get_inbox_ops(char *buffer, const struct kernel_param *kp) {
  unsigned long flags;
  int ret;

  spin_lock_irqsave(&g_msgpool_ctx.last_msg_lock, flags);
  if (strlen(g_msgpool_ctx.last_msg) == 0) {
    ret = scnprintf(buffer, PAGE_SIZE, "Last msg: (empty)\n");
  } else {
    ret = scnprintf(buffer, PAGE_SIZE, "Last msg: %s\n", g_msgpool_ctx.last_msg);
  }
  spin_unlock_irqrestore(&g_msgpool_ctx.last_msg_lock, flags);
  return ret;
}
static const struct kernel_param_ops inbox_ops = {
    .get = param_get_inbox_ops,
};
module_param_cb(inbox, &inbox_ops, NULL, 0444);
MODULE_PARM_DESC(inbox, "последнее обработанное сообщение");

static int param_get_stats_ops(char *buffer, const struct kernel_param *kp) {
  unsigned long flags;
  spin_lock_irqsave(&g_msgpool_ctx.queue.lock, flags);
  unsigned int count = g_msgpool_ctx.queue.count;
  spin_unlock_irqrestore(&g_msgpool_ctx.queue.lock, flags);

  return scnprintf(buffer, PAGE_SIZE,
                   "sent=%d consumed=%d flushed=%d dropped=%d queued=%d "
                   "alloc=%s interval_ms=%d\n",
                   atomic_read(&g_msgpool_ctx.sent_total),
                   atomic_read(&g_msgpool_ctx.consumed_total),
                   atomic_read(&g_msgpool_ctx.flushed_total),
                   atomic_read(&g_msgpool_ctx.dropped_total), count,
                   g_msgpool_ctx.alloc_type == 0 ? "kmem_cache" : "mempool",
                   g_msgpool_ctx.interval_ms);
}
static const struct kernel_param_ops stats_ops = {
    .get = param_get_stats_ops,
};
module_param_cb(stats, &stats_ops, NULL, 0444);
MODULE_PARM_DESC(stats, "статистика");

static int param_set_flash_ops(const char *val, const struct kernel_param *kp) {
  int flash;
  int ret;

  ret = kstrtouint(val, 0, &flash);
  if (ret)
    return ret;

  if (flash != 1) {
    pr_err("Invalid flash %u.\n", flash);
    return -EINVAL;
  }

  msgpool_ctx_flush();
  return MP_OK;
}
static const struct kernel_param_ops flash_ops = {
    .set = param_set_flash_ops,
};
module_param_cb(flush, &flash_ops, NULL, 0200);
MODULE_PARM_DESC(flush, "сброс очереди");

int g_interval_ms = TIMER_INTERVAL_MS;
static int param_set_interval_ops(const char *val,
                                  const struct kernel_param *kp) {
  int interval;
  int ret;

  ret = kstrtouint(val, 0, &interval);
  if (ret) {
    pr_err("Invalid interval_ms %s\n", val);
    return ret;
  }

  if (interval < MIN_INTERVAL_MS || interval > MAX_INTERVAL_MS) {
    pr_err("Invalid interval_ms %u.\n", interval);
    return -EINVAL;
  }

  g_interval_ms = interval;
  if (g_is_init == 0) {
    return MP_OK;
  }

  g_msgpool_ctx.interval_ms = interval;
  pr_info("Interval_ms: %u\n", interval);
  mod_timer(&g_msgpool_ctx.consumer_timer,
            jiffies + msecs_to_jiffies(g_msgpool_ctx.interval_ms));

  return MP_OK;
}
static const struct kernel_param_ops interval_ops = {
    .set = param_set_interval_ops,
    .get = param_get_uint,
};
module_param_cb(interval_ms, &interval_ops, &g_msgpool_ctx.interval_ms, 0644);
MODULE_PARM_DESC(interval_ms, "интервал таймера");
