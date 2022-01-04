#pragma once

#include "HashMap.h"

typedef struct Node Node;

Node* node_new();

void node_free(Node* node);

void node_recursive_free(Node* node);

void node_set_to_delete(Node* node);

HashMap* node_get_children(Node* node);

int node_get_waiting_writers(Node* node);

void start_reading(Node* node);

void finish_reading(Node* node);

void start_writing(Node* node);

void finish_writing(Node* node);

void start_cleaning(Node* node);
