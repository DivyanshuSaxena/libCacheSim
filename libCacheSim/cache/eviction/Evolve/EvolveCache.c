//
//  a module that supports evolution of cache algorithms via LLM calls.
//  currently implements the simple LRU algorithm.
// 

#include <glib.h>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"
#include "EvolveInternal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void EvolveCache_free(cache_t *cache);
static bool EvolveCache_get(cache_t *cache, const request_t *req);
static cache_obj_t *EvolveCache_find(cache_t *cache, const request_t *req,
                                     const bool update_cache);
static cache_obj_t *EvolveCache_insert(cache_t *cache, const request_t *req);
static cache_obj_t *EvolveCache_to_evict(cache_t *cache, const request_t *req);
static void EvolveCache_evict(cache_t *cache, const request_t *req);
static bool EvolveCache_remove(cache_t *cache, const obj_id_t obj_id);
static void EvolveCache_print_cache(const cache_t *cache);

/* internal functions -- derived from LFU */
static inline void free_freq_node(void *list_node);
static inline void free_freq_obj(void *list_node);
static inline void update_min_freq(EvolveCache_params_t *params);

/* internal functions -- common place to update metadata */
static void update_metadata_access(cache_t *cache, cache_obj_t *obj);
static void update_metadata_insert(cache_t *cache, cache_obj_t *obj);
static void update_metadata_evict(cache_t *cache, cache_obj_t *obj);

/* internal functions -- helpers for freq_list ops */
static void remove_freq_list_obj_from_list(evolve_freq_node_t *freq_node,
                                           cache_obj_t *cache_obj);
static void append_freq_list_obj_to_tail(evolve_freq_node_t *freq_node,
                                         cache_obj_t *cache_obj);

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
cache_t *EvolveCache_init(const common_cache_params_t ccache_params,
                          const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("EvolveCache", ccache_params, cache_specific_params);
  cache->cache_init = EvolveCache_init;
  cache->cache_free = EvolveCache_free;
  cache->get = EvolveCache_get;
  cache->find = EvolveCache_find;
  cache->insert = EvolveCache_insert;
  cache->evict = EvolveCache_evict;
  cache->remove = EvolveCache_remove;
  cache->to_evict = EvolveCache_to_evict;
  cache->get_occupied_byte = cache_get_occupied_byte_default;
  cache->can_insert = cache_can_insert_default;
  cache->get_n_obj = cache_get_n_obj_default;
  cache->print_cache = EvolveCache_print_cache;

  if (ccache_params.consider_obj_metadata) {
    cache->obj_md_size = 8 * 2;
  } else {
    cache->obj_md_size = 0;
  }

  EvolveCache_params_t *params = malloc(sizeof(EvolveCache_params_t));
  params->q_head = NULL;
  params->q_tail = NULL;
  cache->eviction_params = params;

  // Initialize the frequency map
  params->freq_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                           (GDestroyNotify)free_freq_node);

  // Add the first frequency node -- no object in its list.
  evolve_freq_node_t *freq_node = my_malloc_n(evolve_freq_node_t, 1);
  freq_node->freq = 1;
  freq_node->n_obj = 0;
  freq_node->first_obj = NULL;
  freq_node->last_obj = NULL;

  params->min_freq = 1;
  params->max_freq = 1;
  params->min_freq_node = freq_node;
  g_hash_table_insert(params->freq_map, GSIZE_TO_POINTER(1), freq_node);

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void EvolveCache_free(cache_t *cache) {
  // Free the frequency map before freeing the cache.
  EvolveCache_params_t *params =
      (EvolveCache_params_t *)(cache->eviction_params);
  g_hash_table_destroy(params->freq_map);
  my_free(sizeof(EvolveCache_params_t), params);
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
static bool EvolveCache_get(cache_t *cache, const request_t *req) {
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
static cache_obj_t *EvolveCache_find(cache_t *cache, const request_t *req,
                                     const bool update_cache) {

  if (cache->n_req % 10000 == 0) {
    printf("EvolveCache_find: n_req = %ld\n", cache->n_req);
  }

  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;
  cache_obj_t *cache_obj = cache_find_base(cache, req, update_cache);

  if (cache_obj && likely(update_cache)) {
    // Update the metadata of the cache for the accessed object.
    update_metadata_access(cache, cache_obj);

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
static cache_obj_t *EvolveCache_insert(cache_t *cache, const request_t *req) {
  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;

  cache_obj_t *obj = cache_insert_base(cache, req);

  // Prepend the object to the head of the queue for fast LRU access.
  prepend_obj_to_head(&params->q_head, &params->q_tail, obj);

  // Update the metadata of the cache for the inserted object.
  update_metadata_insert(cache, obj);

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
static cache_obj_t *EvolveCache_to_evict(cache_t *cache, const request_t *req) {
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
static void EvolveCache_evict(cache_t *cache, const request_t *req) {
  // Make the eviction LLM call.
  cache_obj_t *obj_to_evict = EvolveCache_llm(cache);
  if (obj_to_evict == NULL) {
    // If the LLM call returns NULL, we cannot evict anything.
    return;
  }

  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;

  // Update metadata of the cache for the object to be evicted.
  update_metadata_evict(cache, obj_to_evict);

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
static void EvolveCache_remove_obj(cache_t *cache, cache_obj_t *obj) {
  assert(obj != NULL);

  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;

  // Update the metadata of the cache for the removed object.
  update_metadata_evict(cache, obj);

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
static bool EvolveCache_remove(cache_t *cache, const obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    return false;
  }

  EvolveCache_remove_obj(cache, obj);
  return true;
}

static void EvolveCache_print_cache(const cache_t *cache) {
  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;
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

// ***********************************************************************
// ****                                                               ****
// ****                  cache internal functions                     ****
// ****                                                               ****
// ***********************************************************************
static inline void free_freq_node(void *list_node) {
  my_free(sizeof(evolve_freq_node_t), list_node);
}

static inline void free_freq_obj(void *list_node) {
  my_free(sizeof(freq_obj_t), list_node);
}

static inline void update_min_freq(EvolveCache_params_t *params) {
  int64_t old_min_freq = params->min_freq;
  for (int64_t freq = params->min_freq + 1; freq <= params->max_freq; freq++) {
    evolve_freq_node_t *node =
        g_hash_table_lookup(params->freq_map, GSIZE_TO_POINTER(freq));
    if (node != NULL && node->n_obj > 0) {
      params->min_freq = freq;
      params->min_freq_node = node;
      VVVERBOSE("update min_freq to %ld %d %p %p\n", node->freq, node->n_obj,
                node->first_obj, node->last_obj);
      break;
    }
  }
  DEBUG_ASSERT(params->min_freq > old_min_freq);
}

/**
 * @brief update the metadata of the object
 * this function is called when an object is accessed
 * it updates the frequency of the object and moves it to the new frequency node
 *
 * @param cache
 * @param obj
 */
static void update_metadata_access(cache_t *cache, cache_obj_t *obj) {
  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;

  // Update the frequency of the object
  obj->evolve.freq += 1;
  if ((int64_t)params->max_freq < obj->evolve.freq) {
    params->max_freq = obj->evolve.freq;
  }

  // Find the frequency node this object belongs to and update info
  gpointer old_key = GSIZE_TO_POINTER(obj->evolve.freq - 1);
  evolve_freq_node_t *old_node = g_hash_table_lookup(params->freq_map, old_key);
  DEBUG_ASSERT(old_node != NULL);
  DEBUG_ASSERT(old_node->freq == obj->evolve.freq - 1);
  DEBUG_ASSERT(old_node->n_obj > 0);
  old_node->n_obj -= 1;

  // Remove the object from the linked list of the old frequency node.
  remove_freq_list_obj_from_list(old_node, obj);

  // Find the new frequency node this object should move to, and update info.
  gpointer new_key = GSIZE_TO_POINTER(obj->evolve.freq);
  evolve_freq_node_t *new_node = g_hash_table_lookup(params->freq_map, new_key);
  if (new_node == NULL) {
    new_node = my_malloc_n(evolve_freq_node_t, 1);
    memset(new_node, 0, sizeof(evolve_freq_node_t));
    new_node->freq = obj->evolve.freq;
    g_hash_table_insert(params->freq_map, new_key, new_node);
    VVVERBOSE("allocate new %ld %d %p %p\n", new_node->freq, new_node->n_obj,
              new_node->first_obj, new_node->last_obj);
  } else {
    // it could be new_node is empty
    DEBUG_ASSERT(new_node->freq == obj->evolve.freq);
  }

  // Append the object to the new frequency node.
  append_freq_list_obj_to_tail(new_node, obj);
  new_node->n_obj += 1;

  // If the old frequency node has no objects left, we need to update min_freq
  // and remove the old frequency node from the hash table.
  if (old_node->n_obj == 0) {
    // if the old freq_node has one object and is the min_freq_node, after
    // removing this object, the freq_node will have no object,
    // then we should update min_freq to the new freq
    if ((int64_t)params->min_freq == old_node->freq) {
      update_min_freq(params);
    }

    // Remove the old frequency node from the hash table if its frequency is not
    // 1 because we always keep the frequency node with frequency 1 to add new
    // objects
    if (old_node->freq != 1) {
      g_hash_table_remove(params->freq_map, old_key);
    }
  }
}

/**
 * @brief update the metadata of the object
 * this function is called when an object is inserted
 * it adds it to the frequency node with frequency 1
 *
 * @param cache
 * @param obj
 */
static void update_metadata_insert(cache_t *cache, cache_obj_t *obj) {
  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;

  // Initialize the frequency of the object to 1
  obj->evolve.freq = 1;

  // Find the frequency node with frequency 1.
  evolve_freq_node_t *freq_node =
      g_hash_table_lookup(params->freq_map, GSIZE_TO_POINTER(1));
  if (freq_node == NULL) {
    // If it does not exist, create a new frequency node.
    freq_node = my_malloc_n(evolve_freq_node_t, 1);
    freq_node->freq = 1;
    freq_node->n_obj = 0;
    freq_node->first_obj = NULL;
    freq_node->last_obj = NULL;

    // Insert the new frequency node into the frequency map.
    g_hash_table_insert(params->freq_map, GSIZE_TO_POINTER(1), freq_node);
  }

  // Create a new freq_obj_t for the inserted object.
  freq_obj_t *freq_obj = my_malloc_n(freq_obj_t, 1);
  freq_obj->obj = obj;
  freq_obj->prev = NULL;
  freq_obj->next = NULL;

  // Append the freq_obj_t to the frequency node's list.
  append_freq_list_obj_to_tail(freq_node, obj);
  if (freq_node->n_obj == 1) {
    // If this is the first object in the frequency node, update min_freq.
    params->min_freq = 1;
    params->min_freq_node = freq_node;
  }
}

/**
 * @brief update the metadata of the object
 * this function is called when an object is evicted
 *
 * @param cache
 * @param obj
 */
static void update_metadata_evict(cache_t *cache, cache_obj_t *obj) {
  EvolveCache_params_t *params = (EvolveCache_params_t *)cache->eviction_params;

  // Find the frequency node with object frequency.
  int32_t obj_freq = obj->evolve.freq;
  evolve_freq_node_t *freq_node =
      g_hash_table_lookup(params->freq_map, GSIZE_TO_POINTER(obj_freq));

  // Make sure the LLM does not return an object that is not in the cache.
  DEBUG_ASSERT(freq_node != NULL);
  DEBUG_ASSERT(freq_node->freq == obj_freq);
  DEBUG_ASSERT(freq_node->n_obj > 0);

  // Remove the object from the linked list of the frequency node.
  remove_freq_list_obj_from_list(freq_node, obj);
  freq_node->n_obj -= 1;

  // If the frequency node has no objects left, we need to delete the node
  // and update the min_freq if necessary.
  if (freq_node->n_obj == 0) {
    if ((int64_t)params->min_freq == freq_node->freq) {
      update_min_freq(params);
    }

    g_hash_table_remove(params->freq_map, GSIZE_TO_POINTER(freq_node->freq));
  }

}

/** remove the freq object from the built-in doubly linked list at freq_node
 *  that points to the cache_obj_t passed
 *
 * @param freq_node
 * @param cache_obj
 */
static void remove_freq_list_obj_from_list(evolve_freq_node_t *freq_node,
                                    cache_obj_t *cache_obj) {
  // Iterate through the frequency object list to find the object that points to
  // the cache_obj
  freq_obj_t *freq_obj = freq_node->first_obj;
  while (freq_obj != NULL && freq_obj->obj != cache_obj) {
    freq_obj = freq_obj->next;
  }

  if (freq_obj == NULL) {
    // The object is not in the frequency list
    return;
  }

  // Update the linked list in freq_node.
  if (freq_obj->prev != NULL) freq_obj->prev->next = freq_obj->next;
  if (freq_obj->next != NULL) freq_obj->next->prev = freq_obj->prev;

  freq_obj->prev = NULL;
  freq_obj->next = NULL;

  // Delete the freq_obj.
  free_freq_obj(freq_obj);
}

/**
 * append the cache_obj to the tail of the doubly linked list of the freq_node.
 * @param freq_node
 * @param cache_obj
 */
static void append_freq_list_obj_to_tail(evolve_freq_node_t *freq_node,
                                  cache_obj_t *cache_obj) {
  // Construct a new freq_obj_t
  freq_obj_t *freq_obj = my_malloc_n(freq_obj_t, 1);
  memset(freq_obj, 0, sizeof(freq_obj_t));

  freq_obj->obj = cache_obj;
  freq_obj->next = NULL;
  freq_obj->prev = freq_node->last_obj;

  if (freq_node->first_obj == NULL) {
    // the list is empty
    DEBUG_ASSERT(freq_node->last_obj == NULL);
    freq_node->first_obj = freq_obj;
  }

  if (freq_node->last_obj != NULL) {
    // the list has at least one element
    freq_node->last_obj->next = freq_obj;
  }

  freq_node->last_obj = freq_obj;
  freq_node->n_obj += 1;
  DEBUG_ASSERT(freq_node->n_obj > 0);
}

#ifdef __cplusplus
}
#endif
