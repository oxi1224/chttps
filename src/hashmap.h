#pragma once
#include <stdio.h>

typedef struct {
  const char *key;
  void *value;
} hm_entry;

typedef struct {
  hm_entry *entries;
  size_t size;
  size_t capacity;
} hash_map;

hash_map *hm_create();
const char *hm_set(hash_map *hm, const char *key, void *value);
void hm_remove(hash_map *hm, const char *key);
void *hm_get(hash_map *hm, const char *key);
void hm_free(hash_map *hm);
