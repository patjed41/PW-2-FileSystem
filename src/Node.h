#pragma once

#include "HashMap.h"

typedef struct Node Node; // structure representing folder

// Returns pointer to newly created Node.
Node* node_new();

// Frees [node]'s memory.
void node_free(Node* node);

// Frees memory od [node] and all his descendants.
void node_recursive_free(Node* node);

// Marks node as "to_delete". Last finishing reader will free its memory.
void node_set_to_delete(Node* node);

// Returns HashMap containing children of [node].
HashMap* node_get_children(Node* node);

// Returns number of waiting writers.
int node_get_waiting_writers(Node* node);

void start_reading(Node* node);

void finish_reading(Node* node);

void start_writing(Node* node);

void finish_writing(Node* node);

void start_cleaning(Node* node);
