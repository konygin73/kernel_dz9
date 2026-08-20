#include "pc_mod.h"

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Konygin");
MODULE_DESCRIPTION("Kernel memory");
MODULE_VERSION("1.0");

static void timer_callback(struct timer_list *t) {
  int ret;
  char msg[MSG_TEXT_MAX];

  pr_info("Timer step\n");
  do {
    ret = msgpool_ctx_get(msg, sizeof(msg));
    if (ret == MP_BUSY) {
      pr_info("msgpool BUSY %d\n", ret);
    }
  } while(ret == MP_OK);

  mod_timer(&g_msgpool_ctx.consumer_timer,
            jiffies + msecs_to_jiffies(g_msgpool_ctx.interval_ms));
}

int g_is_init = 0;
static int __init my_module_init(void) {
  int ret = msgpool_ctx_init();
  if (ret != MP_OK) {
    pr_err("Init error: %d\n", ret);
    return ret;
  }
  pr_info("Модуль загружен. HZ=%d (тик потребителя каждые %u мс)\n", HZ,
          g_msgpool_ctx.interval_ms);
  timer_setup(&g_msgpool_ctx.consumer_timer, timer_callback, 0);
  mod_timer(&g_msgpool_ctx.consumer_timer,
            jiffies + msecs_to_jiffies(g_msgpool_ctx.interval_ms));
  g_is_init = 1;

  return ret;
}

static void __exit my_module_exit(void) {
  timer_shutdown_sync(&g_msgpool_ctx.consumer_timer);
  msgpool_ctx_flush();

  if (g_msgpool_ctx.msg_pool) {
    mempool_destroy(g_msgpool_ctx.msg_pool);
  }
  if (g_msgpool_ctx.msg_cache) {
    kmem_cache_destroy(g_msgpool_ctx.msg_cache);
  }
  pr_info("Module unloaded.\n");
}

module_init(my_module_init);
module_exit(my_module_exit);
