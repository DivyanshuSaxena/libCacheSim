#include "EvolveComplete.h"

#include <algorithm>
#include <list>

void EvolveComplete::update_metadata_access(const cache_t *cache,
                                            cache_obj_t *obj) {
  // Find the associated object metadata in the map.
  std::shared_ptr<EvolveComplete_obj_metadata_t> obj_metadata_ptr;
  auto it = obj_metadata_map.find(obj->obj_id);
  if (it != obj_metadata_map.end()) {
    obj_metadata_ptr = it->second;
  } else {
    // Object not found in the metadata map, nothing to update.
    VERBOSE("Object %lu not found in metadata map.\n", obj->obj_id);
    return;
  }

  // Object found, update count metadata of obj and of the cache.
  int32_t prev_count = obj_metadata_ptr->count;
  obj_metadata_ptr->count += 1;
  this->update_counts(obj_metadata_ptr->count);
  this->remove_count(prev_count);

  // Recompute the count statistics.
  this->calculate_counts_percentiles();

  // Compute the delta between current access and last access time.
  int32_t delta = cache->n_req - obj_metadata_ptr->last_access_vtime;
  obj_metadata_ptr->add_delta(delta);

  // Update the last access time of the object.
  obj_metadata_ptr->last_access_vtime = cache->n_req;
}

void EvolveComplete::update_metadata_insert(const cache_t *cache, cache_obj_t *obj) {
  // Add the object metadata to the map.
  int64_t obj_size = obj->obj_size;
  auto obj_metadata = std::make_shared<EvolveComplete_obj_metadata_t>(
      1, cache->n_req, obj_size);
  obj_metadata_map[obj->obj_id] = obj_metadata;

  // Update the counts, ages, and sizes lists.
  this->update_counts(obj_metadata->count);
  this->update_ages(cache->n_req);
  this->update_sizes(obj_metadata->size);

  // Recompute the count, age, and size statistics.
  this->calculate_counts_percentiles();
  this->calculate_ages_sizes_percentiles();
}

void EvolveComplete::update_metadata_evict(const cache_t *cache, cache_obj_t *obj) {
  // Find the associated object metadata in the map.
  std::shared_ptr<EvolveComplete_obj_metadata_t> obj_metadata_ptr;
  auto it = obj_metadata_map.find(obj->obj_id);
  if (it != obj_metadata_map.end()) {
    obj_metadata_ptr = it->second;
  } else {
    // Object not found in the metadata map, nothing to update.
    VERBOSE("Object %lu not found in metadata map.\n", obj->obj_id);
    return;
  }

  // Remove the object metadata from the map and add it to the evicted list.
  evicted_obj_metadata.push_back(obj_metadata_ptr);
  if (evicted_obj_metadata.size() > history_size) {
    evicted_obj_metadata.pop_front();  // Keep only the last 'history_size' evictions
  }
  obj_metadata_map.erase(obj->obj_id);

  // Remove the counts, ages, and sizes from the lists.
  this->remove_count(obj_metadata_ptr->count);
  this->remove_age(cache->n_req);
  this->remove_size(obj_metadata_ptr->size);

  // Recompute the count, age, and size statistics.
  this->calculate_counts_percentiles();
  this->calculate_ages_sizes_percentiles();
}

void EvolveComplete::calculate_counts_percentiles() {
  if (counts.empty()) {
    count_p5 = count_p25 = count_p50 = count_p75 = count_p95 = 0;
  } else {
    count_p5 = *(std::next(counts.begin(), counts.size() * 0.05));
    count_p25 = *(std::next(counts.begin(), counts.size() * 0.25));
    count_p50 = *(std::next(counts.begin(), counts.size() * 0.50));
    count_p75 = *(std::next(counts.begin(), counts.size() * 0.75));
    count_p95 = *(std::next(counts.begin(), counts.size() * 0.95));
  }
}

void EvolveComplete::calculate_ages_sizes_percentiles() {
  if (ages.empty()) {
    age_p5 = age_p25 = age_p50 = age_p75 = age_p95 = 0;
  } else {
    age_p5 = *(std::next(ages.begin(), ages.size() * 0.05));
    age_p25 = *(std::next(ages.begin(), ages.size() * 0.25));
    age_p50 = *(std::next(ages.begin(), ages.size() * 0.50));
    age_p75 = *(std::next(ages.begin(), ages.size() * 0.75));
    age_p95 = *(std::next(ages.begin(), ages.size() * 0.95));
  }

  if (sizes.empty()) {
    size_p5 = size_p25 = size_p50 = size_p75 = size_p95 = 0;
  } else {
    size_p5 = *(std::next(sizes.begin(), sizes.size() * 0.05));
    size_p25 = *(std::next(sizes.begin(), sizes.size() * 0.25));
    size_p50 = *(std::next(sizes.begin(), sizes.size() * 0.50));
    size_p75 = *(std::next(sizes.begin(), sizes.size() * 0.75));
    size_p95 = *(std::next(sizes.begin(), sizes.size() * 0.95));
  }
}
