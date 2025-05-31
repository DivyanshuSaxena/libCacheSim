#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"
#include "History.h"

#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>


template <typename T>
class OrderedMultiset : public __gnu_pbds::tree<
    T,
    __gnu_pbds::null_type,
    std::less_equal<T>,
    __gnu_pbds::rb_tree_tag,
    __gnu_pbds::tree_order_statistics_node_update> {

public:
    // Removes one instance of the given value (if present)
    bool remove(const T& value) {
        auto it = this->find(value);
        if (it != this->end()) {
            this->erase(it);
            return true;
        }
        return false;
    }

    // Returns the value at the p-th percentile (e.g. p=0.25 for 25%)
    T percentile(double p) const {
        if (this->empty()) {
            throw std::runtime_error("percentile() called on empty multiset");
        }
        p = std::clamp(p, 0.0, 1.0);
        size_t idx = static_cast<size_t>(p * this->size());
        if (idx >= this->size()) idx = this->size() - 1;
        return *this->find_by_order(idx);
    }
};

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
 */
typedef struct {
  // Keep all objects in a linked list
  std::list<cache_obj_t *> q_list;

  // Head and tail of the linked list
  cache_obj_t *q_head;
  cache_obj_t *q_tail;

  // Pointer to the metadata of the cache (object of the EvolveComplete class)
  void *EvolveComplete_metadata;

  int32_t n_obj;  // Number of objects in the cache
} EvolveComplete_params_t;


cache_obj_t *EvolveComplete_scaffolding(cache_t *cache);

// we need to provide this with:
// - head and tail of the linked list of objects
// - counts, ages, and sizes of the objects in the cache
// - cache_obj_metadata map
// - evicted objects
cache_obj_t *eviction_heuristic(
  cache_obj_t* head, cache_obj_t* tail, 
  OrderedMultiset<int32_t>& counts, OrderedMultiset<int64_t>& ages, OrderedMultiset<int64_t>& sizes,
  std::unordered_map<obj_id_t, std::shared_ptr<EvolveComplete_obj_metadata_t>>& cache_obj_metadata,
  History& history
);