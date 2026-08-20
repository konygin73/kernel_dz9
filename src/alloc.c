#include "pc_mod.h"

struct msg *alloc_msg(struct msgpool_ctx *msgpool) {
  struct msg *ret = NULL;

  if (!msgpool)
    return NULL;

  if (msgpool->alloc_type == T_KMEM_CACHE) {
    ret = kmem_cache_alloc(msgpool->msg_cache, GFP_KERNEL);
  }
  if (msgpool->alloc_type == T_MEMPOOL) {
    ret = mempool_alloc(msgpool->msg_pool, GFP_KERNEL);
  }
  return ret;
}

void free_msg(struct msgpool_ctx *msgpool, struct msg *message) {
  if (!message) {
    return;
  }
  if (msgpool->alloc_type == T_KMEM_CACHE) {
    kmem_cache_free(msgpool->msg_cache, message);
    pr_info("Удаление из KMEM_CACHE аллокатора\n");
  } else if (msgpool->alloc_type == T_MEMPOOL) {
    mempool_free(message, msgpool->msg_pool);
    pr_info("Удаление из MEMPOOL аллокатора\n");
  }
}
