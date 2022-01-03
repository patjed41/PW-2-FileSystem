#pragma once

#include "HashMap.h"

typedef struct Node Node;

Node* node_new(const char* name);

void node_free(Node* node);

void node_recursive_free(Node* node);

HashMap* node_get_children(Node* node);

void start_reading(Node* node);

void finish_reading(Node* node);

void start_writing(Node* node);

void finish_writing(Node* node);