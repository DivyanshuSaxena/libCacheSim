//
//  a module that supports evolution of cache algorithms via LLM calls.
//  currently implements the simple LRU algorithm.
//

#include <glib.h>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"
#include "EvolveComplete.h"

#ifdef __cplusplus
extern "C" {
#endif

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void EvolveComplete_free(cache_t *cache);
static bool EvolveComplete_get(cache_t *cache, const request_t *req);
static cache_obj_t *EvolveComplete_find(cache_t *cache, const request_t *req,
                                        const bool update_cache);
static cache_obj_t *EvolveComplete_insert(cache_t *cache, const request_t *req);
static cache_obj_t *EvolveComplete_to_evict(cache_t *cache,
                                            const request_t *req);
static void EvolveComplete_evict(cache_t *cache, const request_t *req);
static bool EvolveComplete_remove(cache_t *cache, const obj_id_t obj_id);
static void EvolveComplete_print_cache(const cache_t *cache);

// ***********************************************************************

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ****                       init, free, get                         ****
// ***********************************************************************
/**
 * @brief initialize a Evolve cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params Evolve specific parameters, should be NULL
 */
cache_t *EvolveComplete_init(const common_cache_params_t ccache_params,
                             const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("EvolveComplete", ccache_params,
                                     cache_specific_params);
  cache->cache_init = EvolveComplete_init;
  cache->cache_free = EvolveComplete_free;
  cache->get = EvolveComplete_get;
  cache->find = EvolveComplete_find;
  cache->insert = EvolveComplete_insert;
  cache->evict = EvolveComplete_evict;
  cache->remove = EvolveComplete_remove;
  cache->to_evict = EvolveComplete_to_evict;
  cache->get_occupied_byte = cache_get_occupied_byte_default;
  cache->can_insert = cache_can_insert_default;
  cache->get_n_obj = cache_get_n_obj_default;
  cache->print_cache = EvolveComplete_print_cache;

  if (ccache_params.consider_obj_metadata) {
    cache->obj_md_size = 8 * 2;
  } else {
    cache->obj_md_size = 0;
  }

  EvolveComplete_params_t *params = new EvolveComplete_params_t{};

  params->n_obj = 0;
  params->q_head = NULL;
  params->q_tail = NULL;
  params->EvolveComplete_metadata = static_cast<void *>(new EvolveComplete());

  cache->eviction_params = params;

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void EvolveComplete_free(cache_t *cache) {
  // Free the frequency map before freeing the cache.
  EvolveComplete_params_t *params =
      (EvolveComplete_params_t *)(cache->eviction_params);
  my_free(sizeof(EvolveComplete_params_t), params);
  cache_struct_free(cache);
}

/**
 * @brief this function is the user facing API
 * it performs the following logic
 *
 * ```
 * if obj in cache:
 *    update_metadata
 *    return true
 * else:
 *    if cache does not have enough space:
 *        evict until it has space to insert
 *    insert the object
 *    return false
 * ```
 *
 * @param cache
 * @param req
 * @return true if cache hit, false if cache miss
 */
static bool EvolveComplete_get(cache_t *cache, const request_t *req) {
  return cache_get_base(cache, req);
}

// ***********************************************************************
// ****                                                               ****
// ****       developer facing APIs (used by cache developer)         ****
// ****                                                               ****
// ***********************************************************************

/**
 * @brief check whether an object is in the cache
 *
 * @param cache
 * @param req
 * @param update_cache whether to update the cache,
 *  if true, the object is promoted
 *  and if the object is expired, it is removed from the cache
 * @return true on hit, false on miss
 */
static cache_obj_t *EvolveComplete_find(cache_t *cache, const request_t *req,
                                        const bool update_cache) {
  EvolveComplete_params_t *params =
      (EvolveComplete_params_t *)cache->eviction_params;
  cache_obj_t *cache_obj = cache_find_base(cache, req, update_cache);

  if (cache_obj && likely(update_cache)) {
    // Update the metadata of the cache for the accessed object.
    auto *evolve_metadata =
        static_cast<EvolveComplete *>(params->EvolveComplete_metadata);
    evolve_metadata->update_metadata_access(cache, cache_obj);

    // For LRU --> move the object to the head of the queue
    // Makes LRU-type accesses faster.
    move_obj_to_head(&params->q_head, &params->q_tail, cache_obj);
  }
  return cache_obj;
}

/**
 * @brief insert an object into the cache,
 * update the hash table and cache metadata
 * this function assumes the cache has enough space
 * and eviction is not part of this function
 *
 * @param cache
 * @param req
 * @return the inserted object
 */
static cache_obj_t *EvolveComplete_insert(cache_t *cache,
                                          const request_t *req) {
  EvolveComplete_params_t *params =
      (EvolveComplete_params_t *)cache->eviction_params;

  cache_obj_t *obj = cache_insert_base(cache, req);

  // Prepend the object to the head of the queue for fast LRU access.
  prepend_obj_to_head(&params->q_head, &params->q_tail, obj);

  // Update the metadata of the cache for the inserted object.
  auto *evolve_metadata =
      static_cast<EvolveComplete *>(params->EvolveComplete_metadata);
  evolve_metadata->update_metadata_insert(cache, obj);

  return obj;
}

/**
 * @brief find the object to be evicted
 * this function does not actually evict the object or update metadata
 * not all eviction algorithms support this function
 * because the eviction logic cannot be decoupled from finding eviction
 * candidate, so use assert(false) if you cannot support this function
 *
 * @param cache the cache
 * @return the object to be evicted
 */
static cache_obj_t *EvolveComplete_to_evict(cache_t *cache,
                                            const request_t *req) {
  assert(false);
  return NULL;
}

/**
 * @brief evict an object from the cache
 * it needs to call cache_evict_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param req not used
 */
static void EvolveComplete_evict(cache_t *cache, const request_t *req) {
  // Make the eviction LLM call.
  cache_obj_t *obj_to_evict = EvolveComplete_llm(cache);
  if (obj_to_evict == NULL) {
    // If the LLM call returns NULL, we cannot evict anything.
    return;
  }

  // Update the metadata of the cache for the evicted object before eviction.
  EvolveComplete_params_t *params =
      (EvolveComplete_params_t *)cache->eviction_params;
  auto *evolve_metadata =
      static_cast<EvolveComplete *>(params->EvolveComplete_metadata);
  evolve_metadata->update_metadata_evict(cache, obj_to_evict);

  // Remove the object from the linked list of cached objects.
  remove_obj_from_list(&params->q_head, &params->q_tail, obj_to_evict);
  cache_evict_base(cache, obj_to_evict, true);
}

/**
 * @brief remove the given object from the cache
 * note that eviction should not call this function, but rather call
 * `cache_evict_base` because we track extra metadata during eviction
 *
 * and this function is different from eviction
 * because it is used to for user trigger
 * remove, and eviction is used by the cache to make space for new objects
 *
 * it needs to call cache_remove_obj_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param obj
 */
static void EvolveComplete_remove_obj(cache_t *cache, cache_obj_t *obj) {
  assert(obj != NULL);

  // First update the metadata of the cache.
  EvolveComplete_params_t *params =
      (EvolveComplete_params_t *)cache->eviction_params;
  auto *evolve_metadata =
      static_cast<EvolveComplete *>(params->EvolveComplete_metadata);
  evolve_metadata->update_metadata_evict(cache, obj);

  // Remove the object from the linked list of cached objects.
  remove_obj_from_list(&params->q_head, &params->q_tail, obj);
  cache_remove_obj_base(cache, obj, true);
}

/**
 * @brief remove an object from the cache
 * this is different from cache_evict because it is used to for user trigger
 * remove, and eviction is used by the cache to make space for new objects
 *
 * it needs to call cache_remove_obj_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param obj_id
 * @return true if the object is removed, false if the object is not in the
 * cache
 */
static bool EvolveComplete_remove(cache_t *cache, const obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    return false;
  }

  EvolveComplete_remove_obj(cache, obj);
  return true;
}

static void EvolveComplete_print_cache(const cache_t *cache) {
  EvolveComplete_params_t *params =
      (EvolveComplete_params_t *)cache->eviction_params;
  cache_obj_t *cur = params->q_head;
  // print from the most recent to the least recent
  if (cur == NULL) {
    printf("empty\n");
    return;
  }
  while (cur != NULL) {
    printf("%lu->", (unsigned long)cur->obj_id);
    cur = cur->queue.next;
  }
  printf("END\n");
}

#ifdef __cplusplus
}
#endif
