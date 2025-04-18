#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hashmap.h"

#define HM_START_CAPACITY 16
#define FNV_OFFSET_BASIS 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

hash_map *hm_create() {
  hash_map *hm = malloc(sizeof(hash_map));
  if (hm == NULL) return NULL;
  hm->capacity = HM_START_CAPACITY;
  hm->size = 0;
  hm->entries = calloc(HM_START_CAPACITY, sizeof(hm_entry));
  return hm;
}

static uint64_t hash_key(const char *key) {
  uint64_t hash = FNV_OFFSET_BASIS;
  size_t len = strlen(key);
  for (size_t i = 0; i < len; i++) {
    hash *= FNV_PRIME;
    hash ^= (uint64_t)key[i];
  }
  return hash;
}

const char *hm_set(hash_map *hm, const char *key, void *value) {
  if (hm->size + 1 >= hm->capacity / 2) {
    size_t old_capacity = hm->capacity;
    hm->capacity *= 2;
    hm->entries = realloc(hm->entries, sizeof(hm_entry) * hm->capacity);
    // Zero out memory to avoid comparing to garbage
    memset(hm->entries + old_capacity, 0, sizeof(hm_entry) * (hm->capacity - old_capacity));
    if (hm->entries == NULL) return NULL;
  }

  size_t index = hash_key(key) & (uint64_t)(hm->capacity - 1);
  
  while(
    hm->entries[index].key != NULL &&
    strcmp(hm->entries[index].key, key) != 0
  ) {
    index++;
    if (index >= hm->capacity) index = 0;
  }

  if (hm->entries[index].key == NULL) {
    hm_entry entry = {
      .key = key,
      .value = value
    };
    hm->entries[index] = entry;
    hm->size++;
  }
  return key;
}

void hm_remove(hash_map *hm, const char *key) {
  size_t index = hash_key(key) & (uint64_t)(hm->capacity - 1);
  size_t start_index = index; 
  while (strcmp(hm->entries[index].key, key) != 0) {
    index++;
    if (index >= hm->capacity) index = 0;
    // If no key was found (done a full loop)
    if (index == start_index) return;
  }
  free((void*)hm->entries[index].key);
  hm->entries[index].key = NULL;
  free(hm->entries[index].value);
  hm->entries[index].value = NULL;
  hm->size--;
}

void *hm_get(hash_map *hm, const char *key) {
  size_t index = hash_key(key) & (uint64_t)(hm->capacity - 1);
  size_t start_index = index; 
  while (strcmp(hm->entries[index].key, key) != 0) {
    index++;
    if (index >= hm->capacity) index = 0;
    // If no key was found (done a full loop)
    if (index == start_index) return NULL;
  }
  return hm->entries[index].value;
}

void hm_free(hash_map *hm) {
  for (size_t i = 0; i < hm->capacity; i++) {
    if (hm->entries[i].key == NULL) continue;
    free((void*)hm->entries[i].key);
    free((void*)hm->entries[i].value);
  }
  free(hm->entries);
  free(hm);
}
