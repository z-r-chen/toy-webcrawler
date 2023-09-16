#include <stdlib.h>
#include <string.h>

/************************************************************
 *    Title:        djb2 hash function
 *    Author:       Daniel J. Bernstein
 *    Availability: http://www.cse.yorku.ca/~oz/hash.html
 *
 ************************************************************/
unsigned long hash_djb2(char *str) {
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }

    return hash;
}

typedef struct Item {
    unsigned long hash_key;
    char *value;
} Item;

typedef struct Pseudo_HashTable { // a "hash table" that can tolerate duplicate values, so pseudo
    Item **items;
    int capacity;
    int current_available;
} Pseudo_HashTable; 

Item *create_item(unsigned long hash_key, char *link) {
    Item *item = (Item *)malloc(sizeof(Item));
    item->hash_key = hash_key;
    item->value = (char *)malloc(strlen(link) + 1);
    strcpy(item->value, link);

    return item;
}

Pseudo_HashTable *init_table(int size) {
    Pseudo_HashTable *table = (Pseudo_HashTable *)malloc(sizeof(Pseudo_HashTable));

    table->capacity = size;
    table->current_available = 0;
    table->items = (Item **)calloc(table->capacity, sizeof(Item *));

    for (int i = 0; i < table->capacity; ++i) {
        table->items[i] = NULL;
    }

    return table;
}

void free_item(Item *item) {
    free(item->value);
    free(item);
}

void free_table(Pseudo_HashTable *table) {
    for (int i = 0; i < table->capacity; ++i) {
        Item *item = table->items[i];
        if (item != NULL) {
            free_item(item);
        }
    }
    free(table->items);
    free(table);
}

void insert(Pseudo_HashTable *table, char *link) {
    unsigned long local_hash = hash_djb2(link);
    Item *item = create_item(local_hash, link);
    int cur = table->current_available;

    if (table->current_available < table->capacity) {
        table->items[cur] = item;
        table->current_available++;
    }
}

char *search(Pseudo_HashTable *table, unsigned long hash_key) {
    char *res = NULL;
    for (int i = 0; i < table->current_available; ++i) {
        if (table->items[i]->hash_key == hash_key) {
            res = table->items[i]->value;
        }
    }

    return res;
}