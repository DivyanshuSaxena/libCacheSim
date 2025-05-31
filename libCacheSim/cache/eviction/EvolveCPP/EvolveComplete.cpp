#include "EvolveComplete.h"

#include <algorithm>
#include <list>

void EvolveComplete::update_metadata_access(const cache_t *cache,
                                            cache_obj_t *obj) {
  // Find the associated object metadata in the map.
  std::shared_ptr<EvolveComplete_obj_metadata_t> obj_metadata_ptr;
  auto it = cache_obj_metadata.find(obj->obj_id);
  if (it != cache_obj_metadata.end()) {
    obj_metadata_ptr = it->second;
  } else {
    // Object not found in the metadata map, nothing to update.
    VERBOSE("Object %lu not found in metadata map.\n", obj->obj_id);
    return;
  }

  // Object found, update count metadata of obj and of the cache.
  int32_t prev_count = obj_metadata_ptr->count;
  obj_metadata_ptr->count += 1;
  this->counts.insert(obj_metadata_ptr->count);
  this->counts.remove(prev_count);

  // Compute the delta between current access and last access time.
  int32_t delta = cache->n_req - obj_metadata_ptr->last_access_vtime;
  obj_metadata_ptr->add_delta(delta);

  // Update the last access time of the object.
  obj_metadata_ptr->last_access_vtime = cache->n_req;
}

void EvolveComplete::update_metadata_insert(const cache_t *cache, cache_obj_t *obj) {
  // Add the object metadata to the map.
  int64_t obj_size = obj->obj_size;
  auto new_obj_metadata = std::make_shared<EvolveComplete_obj_metadata_t>(1, cache->n_req, obj_size);
  cache_obj_metadata[obj->obj_id] = new_obj_metadata;

  // Update the counts, ages, and sizes lists.
  this->counts.insert(new_obj_metadata->count);
  this->addition_vtime_timestamps.insert(cache->n_req);
  this->sizes.insert(new_obj_metadata->size);
}

void EvolveComplete::update_metadata_evict(const cache_t *cache, cache_obj_t *obj) {
  // Find the associated object metadata in the map.
  std::shared_ptr<EvolveComplete_obj_metadata_t> obj_metadata_ptr;
  auto it = cache_obj_metadata.find(obj->obj_id);
  if (it != cache_obj_metadata.end()) {
    obj_metadata_ptr = it->second;
  } else {
    // Object not found in the metadata map, nothing to update.
    VERBOSE("Object %lu not found in metadata map.\n", obj->obj_id);
    return;
  }

  // insert to history, delete from cache
  history.insert(obj->obj_id, obj->obj_size, obj_metadata_ptr->count, cache->n_req - obj_metadata_ptr->addition_to_cache_vtime);
  cache_obj_metadata.erase(obj->obj_id);

  // Remove the counts, ages, and sizes from the lists.
  this->counts.remove(obj_metadata_ptr->count);
  this->addition_vtime_timestamps.remove(obj_metadata_ptr->addition_to_cache_vtime);
  this->sizes.remove(obj_metadata_ptr->size);
}