#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"
#include "History.h"
#include "Percentile.h"

class EvolveComplete_obj_metadata_t {
  public:
  int32_t count;              // number of times object accessed
  int64_t last_access_vtime;  // last vtime object accessed

  // updated once when object is inserted
  int64_t size;  // bytes
  int64_t addition_to_cache_vtime;  // vtime of addition to cache

  const size_t num_deltas;    // Number of deltas recorded for the object
  std::list<int32_t> deltas;  // List of last num_deltas deltas for the object

  EvolveComplete_obj_metadata_t(int32_t c, int64_t l, int64_t s,
                                int32_t d = 20)
      : count(c), last_access_vtime(l), size(s), addition_to_cache_vtime(l), num_deltas(d) {
    deltas = std::list<int32_t>();
  }

  ~EvolveComplete_obj_metadata_t() { deltas.clear(); }

  void add_delta(int32_t delta) {
    deltas.push_back(delta);
    if (deltas.size() > num_deltas) {
      deltas.pop_front();
    }
  }
};

class EvolveComplete {
 public:
  // OBJECTS IN CACHE: object id -> metadata.
  std::unordered_map<obj_id_t, std::shared_ptr<EvolveComplete_obj_metadata_t>> cache_obj_metadata;
  
  // recently evicted objects
  History history;

  // store the counts, addition times, and sizes of all objects in the cache.
  OrderedMultiset<int32_t> counts;
  OrderedMultiset<int64_t> addition_vtime_timestamps;
  OrderedMultiset<int64_t> sizes;

  // Functions
  EvolveComplete(size_t h = 100) : history(h) {};
  ~EvolveComplete() {}

  /**
   * @brief update the metadata of the cache when the object is accessed.
   * @param cache
   * @param obj
   */
  void update_metadata_access(const cache_t *cache, cache_obj_t *obj);

  /**
   * @brief update the metadata of cache on insert.
   * @param cache
   * @param obj
   */
  void update_metadata_insert(const cache_t *cache, cache_obj_t *obj);

  /**
   * @brief update the metadata of the cache when an object is evicted
   * Removes the respective counts, deltas, and sizes from the lists
   * @param cache
   * @param obj
   */
  void update_metadata_evict(const cache_t *cache, cache_obj_t *obj);
};

/** @brief EvolveComplete eviction parameters structure
 *
 * This structure contains the parameters for the EvolveComplete eviction
 * algorithm. It includes a linked list of cache objects, head and tail
 * pointers, and metadata.
 * 
 * TODO: Move q_head, q_tail and n_obj into EvolveComplete.
 */
typedef struct {
  // Head and tail of the linked list
  cache_obj_t *q_head;
  cache_obj_t *q_tail;

  // Pointer to the metadata of the cache (object of the EvolveComplete class)
  void *EvolveComplete_metadata;

  int32_t n_obj;  // Number of objects in the cache
} EvolveComplete_params_t;


class cache_ptr {
public:
    cache_obj_t* obj;
    const std::unordered_map<obj_id_t,
          std::shared_ptr<EvolveComplete_obj_metadata_t>>* map;   // only the map ref

    cache_ptr() : obj(nullptr), map(nullptr) {}
    cache_ptr(cache_obj_t* o,
              const std::unordered_map<obj_id_t,
                    std::shared_ptr<EvolveComplete_obj_metadata_t>>& m)
        : obj(o), map(&m) {}

    /* navigation */
    cache_ptr next() const { return cache_ptr(obj ? obj->queue.next : nullptr, *map); }
    cache_ptr prev() const { return cache_ptr(obj ? obj->queue.prev : nullptr, *map); }

    /* metadata access – fetched lazily */
    const EvolveComplete_obj_metadata_t& meta() const {
        return *map->at(obj->obj_id);
    }

    /* comparators */
    bool operator==(const cache_ptr& other) const { return obj == other.obj; }
    bool operator!=(const cache_ptr& other) const { return obj != other.obj; }

    /* nullptr equality */
    bool operator==(std::nullptr_t)       const { return obj == nullptr; }
    bool operator!=(std::nullptr_t)       const { return obj != nullptr; }

    /* handy shorthands */
    obj_id_t id() const { return obj->obj_id; }
    int32_t count() const        { return meta().count; }
    int64_t last_access() const  { return meta().last_access_vtime; }
    int64_t size() const         { return meta().size; }
    int64_t added_at() const     { return meta().addition_to_cache_vtime; }

    bool         is_null() const { return obj == nullptr; }
    cache_obj_t* raw()     const { return obj; }
};


cache_obj_t *EvolveComplete_scaffolding(cache_t *cache, const request_t *req, int32_t num_candidates = 100);

// we need to provide this with:
// - head and tail of the linked list of objects
// - counts, ages, and sizes of the objects in the cache
// - cache_obj_metadata map
// - evicted objects
cache_ptr eviction_heuristic(
  cache_ptr head, cache_ptr tail, uint64_t current_time, 
  OrderedMultiset<int32_t>& counts, AgePercentileView<int64_t> ages, OrderedMultiset<int64_t>& sizes,
  History& history
);