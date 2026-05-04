#include "EvolveComplete.h"
#include <iostream>
#include <algorithm>

#include "LLMCode.h"

cache_obj_t *EvolveComplete_scaffolding(cache_t *cache, const request_t *req, int32_t num_candidates) {
    EvolveComplete_params_t *params = (EvolveComplete_params_t *)cache->eviction_params;
    if (params->q_tail == NULL) return NULL;

    auto evolve_metadata = static_cast<EvolveComplete *>(params->EvolveComplete_metadata);

    // Walk backward from tail to collect num_candidates objects
    std::vector<cache_obj_t*> raw_candidates;
    cache_obj_t *current = params->q_tail;
    while (current != NULL && (int32_t)raw_candidates.size() < num_candidates) {
        raw_candidates.push_back(current);
        current = current->queue.prev;
    }

    // Build CandidateInfo vector from collected objects
    std::vector<CandidateInfo> candidates;
    candidates.reserve(raw_candidates.size());
    for (auto *obj : raw_candidates) {
        auto it = evolve_metadata->cache_obj_metadata.find(obj->obj_id);
        if (it != evolve_metadata->cache_obj_metadata.end()) {
            auto &meta = *(it->second);
            candidates.push_back({
                obj->obj_id,
                meta.count,
                meta.last_access_vtime,
                meta.size,
                meta.addition_to_cache_vtime
            });
        }
    }

    if (candidates.empty()) return params->q_tail;

    int idx = select_victim(
        candidates, req->n_req,
        evolve_metadata->counts,
        AgePercentileView<int64_t>(evolve_metadata->addition_vtime_timestamps, cache->n_req),
        evolve_metadata->sizes,
        evolve_metadata->history
    );

    // Clamp index to valid range
    idx = std::max(0, std::min(idx, (int)candidates.size() - 1));

    return raw_candidates[idx];
}
