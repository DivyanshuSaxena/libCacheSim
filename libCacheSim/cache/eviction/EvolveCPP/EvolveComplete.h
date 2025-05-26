#pragma once

#include <list>
#include <memory>
#include <unordered_map>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"

class EvolveComplete {
 public:
  class EvolveComplete_obj_metadata_t {
   public:
    int32_t count;              // Number of times the object has been accessed
    int64_t last_access_vtime;  // Last time the object was accessed

    // Updated only when the object is inserted.
    int64_t size;  // Size of the object in bytes
    int64_t age;   // Age (virtual) of the object in the cache

    const size_t num_deltas;    // Number of deltas recorded for the object
    std::list<int32_t> deltas;  // List of last num_deltas deltas for the object

    EvolveComplete_obj_metadata_t(int32_t c, int64_t l, int64_t s,
                                  int32_t d = 20)
        : count(c), last_access_vtime(l), size(s), age(l), num_deltas(d) {
      deltas = std::list<int32_t>();
    }

    ~EvolveComplete_obj_metadata_t() { deltas.clear(); }

    void add_delta(int32_t delta) {
      deltas.push_back(delta);
      if (deltas.size() > num_deltas) {
        deltas.pop_front();  // Keep only the last 20 deltas
      }
    }
  };

  // Keep a hashmap of object id to its metadata.
  std::unordered_map<obj_id_t, std::shared_ptr<EvolveComplete_obj_metadata_t>>
      obj_metadata_map;

  // Keep track of the metadata of recently evicted objects.
  std::list<std::shared_ptr<EvolveComplete_obj_metadata_t>>
      evicted_obj_metadata;
  const size_t history_size;  // Number of evicted objects to keep track of

  // List to store the counts, ages, and size, of all objects in the cache.
  std::list<int32_t> counts;
  std::list<int64_t> ages;
  std::list<int64_t> sizes;

  // Statistics for the cache -- need to be updated after each access.
  int32_t count_p5, count_p25, count_p50, count_p75, count_p95;

  // Statistics for the ages and sizes of objects in the cache.
  // Only updated when the object is inserted or evicted.
  int64_t age_p5, age_p25, age_p50, age_p75, age_p95;
  int64_t size_p5, size_p25, size_p50, size_p75, size_p95;

  // Functions
  EvolveComplete(int32_t h = 100) : history_size(h) {};
  ~EvolveComplete() {}

  /**
   * @brief update the metadata of the cache when the object is accessed.
   *
   * The function is only called when the object is found in the cache.
   * it updates the count and deltas of the object and updates the
   * metadata.
   *
   * It may use some members of the cache struct, that is why const.
   *
   * @param cache
   * @param obj
   */
  void update_metadata_access(const cache_t *cache, cache_obj_t *obj);

  /**
   * @brief update the metadata of the object.
   *
   * This function is called when an object is inserted
   * it adds it to the frequency node with frequency 1
   *
   * It may use some members of the cache struct, that is why const.
   *
   * @param cache
   * @param obj
   */
  void update_metadata_insert(const cache_t *cache, cache_obj_t *obj);

  /**
   * @brief update the metadata of the cache when an object is evicted
   * Removes the respective counts, deltas, and sizes from the lists
   *
   * It may use some members of the cache struct, that is why const.
   *
   * @param cache
   * @param obj
   */
  void update_metadata_evict(const cache_t *cache, cache_obj_t *obj);

 private:
  /**
   * @brief Helper function to update the counts list with the new count.
   * @param count The new count to be added.
   */
  void update_counts(int32_t count) {
    auto it = std::lower_bound(counts.begin(), counts.end(), count);
    counts.insert(it, count);
  }

  /**
   * @brief Helper function to remove the count from the counts list.
   * @param count The count to be removed.
   * @return The number of counts removed (0 or 1).
   */
  int32_t remove_count(int32_t count) {
    auto it = std::lower_bound(counts.begin(), counts.end(), count);
    if (it != counts.end() && *it == count) {
      counts.erase(it);
      return 1;  // Count removed
    }
    return 0;  // Count not found
  }

  /**
   * @brief Helper function to update the ages list with the new age.
   * @param age The new age to be added.
   */
  void update_ages(int32_t age) {
    auto it = std::lower_bound(ages.begin(), ages.end(), age);
    ages.insert(it, age);
  }

  /**
   * @brief Helper function to remove the age from the ages list.
   * @param age The age to be removed.
   * @return The number of ages removed (0 or 1).
   */
  int32_t remove_age(int32_t age) {
    auto it = std::lower_bound(ages.begin(), ages.end(), age);
    if (it != ages.end() && *it == age) {
      ages.erase(it);
      return 1;  // Age removed
    }
    return 0;  // Age not found
  }

  /**
   * @brief Helper function to update the sizes list with the new size.
   * @param size The new size to be added.
   */
  void update_sizes(int32_t size) {
    auto it = std::lower_bound(sizes.begin(), sizes.end(), size);
    sizes.insert(it, size);
  }

  /**
   * @brief Helper function to remove the size from the sizes list.
   * @param size The size to be removed.
   * @return The number of sizes removed (0 or 1).
   */
  int32_t remove_size(int32_t size) {
    auto it = std::lower_bound(sizes.begin(), sizes.end(), size);
    if (it != sizes.end() && *it == size) {
      sizes.erase(it);
      return 1;  // Size removed
    }
    return 0;  // Size not found
  }

  /**
   * @brief Helper function to calculate percentiles for counts.
   * This function should be called at every access, insert, or evict.
   */
  void calculate_counts_percentiles();

  /**
   * @brief Helper function to calculate percentiles for ages and sizes.
   * This function should be called at only inserts and evicts.
   */
  void calculate_ages_sizes_percentiles();
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

/* internal function -- LLM generated code should be here */
cache_obj_t *EvolveComplete_llm(cache_t *cache);