#include "EvolveComplete.h"
#include <iostream> 

cache_obj_t *EvolveComplete_scaffolding(cache_t *cache, const request_t *req, int32_t num_candidates) {
    EvolveComplete_params_t *params = (EvolveComplete_params_t *)cache->eviction_params;
    if (params->q_tail == NULL) return NULL;
    
    auto evolve_metadata = static_cast<EvolveComplete *>(((EvolveComplete_params_t *)cache->eviction_params)->EvolveComplete_metadata);

    // Find the tail-num_candidate object in the linked list.
    int32_t i = 0;
    cache_obj_t *current = params->q_tail;
    while (current->queue.prev != NULL && i < num_candidates) {
        i++;
        current = current->queue.prev;
    }

    auto head_ptr = cache_ptr(current, evolve_metadata->cache_obj_metadata);
    auto tail_ptr = cache_ptr(params->q_tail, evolve_metadata->cache_obj_metadata);

    auto ans = eviction_heuristic(
        head_ptr, tail_ptr, req->n_req,
        evolve_metadata->counts, 
        AgePercentileView<int64_t>(evolve_metadata->addition_vtime_timestamps, cache->n_req), 
        evolve_metadata->sizes,
        evolve_metadata->history
    );
    cache_obj_t* eviction_decision = ans.obj;
    
    assert (eviction_decision != nullptr);
    assert(evolve_metadata->cache_obj_metadata.count(eviction_decision->obj_id) > 0);
    return eviction_decision;
}

template <typename T> using CountsInfo = OrderedMultiset<T>;
template <typename T> using AgeInfo = AgePercentileView<T>;
template <typename T> using SizeInfo = OrderedMultiset<T>;

#ifdef LLM_GENERATED_CODE
    #include "LLMCode.h"
#else
// /**************** LRU ****************/ 
// cache_ptr eviction_heuristic(
//     cache_ptr head, cache_ptr tail, uint64_t current_time,
//     CountsInfo<int32_t>& counts, AgeInfo<int64_t> ages, SizeInfo<int64_t>& sizes,
//     History& history
// ) { 
//     return tail;
// }

// /**************** FIFO ****************/
// cache_ptr eviction_heuristic(
//     cache_ptr head, cache_ptr tail, uint64_t current_time,
//     CountsInfo<int32_t>& counts, AgeInfo<int64_t> ages, SizeInfo<int64_t>& sizes,
//     History& history
// ) {    
//     cache_ptr eviction_candidate = head;

//     for (cache_ptr curr = head; curr != nullptr; curr = curr.next()) {
//         if (curr.added_at() < eviction_candidate.added_at() ) {
//             eviction_candidate = curr;
//         }
//     }
//     return eviction_candidate;
// }

/**************** LFU ****************/
cache_ptr eviction_heuristic(
cache_ptr head, cache_ptr tail, uint64_t current_time,
CountsInfo<int32_t>& counts, AgeInfo<int64_t> ages, SizeInfo<int64_t>& sizes,
History& history
) {    
    cache_ptr eviction_candidate = head;

    for (cache_ptr curr = head; curr != nullptr; curr = curr.next()) {
        if (curr.count() < eviction_candidate.count() ) {
            eviction_candidate = curr;
        }
    }
    return eviction_candidate;
}
#endif