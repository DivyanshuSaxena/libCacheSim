#pragma once

#include <glib.h>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "../../../include/libCacheSim/evictionAlgo.h"

// Element in the list for each frequency node
struct freq_obj;
typedef struct freq_obj {
  cache_obj_t *obj;
  struct freq_obj *prev;
  struct freq_obj *next;
} freq_obj_t;

// Entry for each frequency node
typedef struct evolve_freq_node {
  int64_t freq;
  freq_obj_t *first_obj;
  freq_obj_t *last_obj;
  int32_t n_obj;
} evolve_freq_node_t;

typedef struct {
  // Head of the linked list of cached cache_obj_t objects
  cache_obj_t *q_head;

  // Tail of the linked list of cached cache_obj_t objects
  cache_obj_t *q_tail;

  // Hashmap to store the frequency of each object
  // frequency --> evolve_freq_node_t
  // evolve_freq_node_t is a linked list of freq_obj_t objects with the same frequency.
  // Each freq_obj_t points to a unique cache_obj_t object.
  GHashTable *freq_map;

  // Store the node in hash map with minimum frequency.
  evolve_freq_node_t *min_freq_node;

  // Minimum and maximum frequency
  int64_t min_freq;
  int64_t max_freq;
} EvolveCache_params_t;

/* internal function -- LLM generated code should be here */
cache_obj_t *EvolveCache_llm(cache_t *cache);