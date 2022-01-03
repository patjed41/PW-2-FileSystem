#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "Tree.h"
#include "Node.h"
#include "path_utils.h"
#include "safe_malloc.h"

#include <stdio.h> // DO USUNIECIA

struct Tree {
  Node* root;
};

Tree* tree_new() {
  Tree* tree = (Tree *) safe_malloc(sizeof(Tree));

  tree->root = node_new("");

  return tree;
}

void tree_free(Tree* tree) {
  node_recursive_free(tree->root);

  free(tree);
}

static Node* reach_node(Tree* tree, const char* path) {
  char next_node_name[MAX_FOLDER_NAME_LENGTH + 1];
  const char* subpath = path;
  Node* current_node = tree->root;
  Node* next_node;

  while ((subpath = split_path(subpath, next_node_name)) != NULL) {
    start_reading(current_node);

    next_node = hmap_get(node_get_children(current_node), next_node_name);

    finish_reading(current_node);

    if (next_node == NULL) return NULL;

    current_node = next_node;
  }

  return current_node;
}

char* tree_list(Tree* tree, const char* path) {
  if (!is_path_valid(path)) return NULL;

  Node* node = reach_node(tree, path);
  if (node == NULL) return NULL;

  start_reading(node);

  char* result = make_map_contents_string(node_get_children(node));

  finish_reading(node);

  return result;
}

int tree_create(Tree* tree, const char* path) {
  if (!is_path_valid(path)) return EINVAL;
  if (strcmp(path, "/") == 0) return EEXIST;

  char node_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_parent = make_path_to_parent(path, node_name);
  Node* parent = reach_node(tree, path_to_parent);
  free(path_to_parent);
  if (parent == NULL) return ENOENT;

  int result = 0;

  start_writing(parent);

  if (hmap_get(node_get_children(parent), node_name) != NULL) {
    result = EEXIST;
  }
  else {
    Node* node = node_new(node_name);
    hmap_insert(node_get_children(parent), node_name, node);
  }

  finish_writing(parent);

  return result;
}

int tree_remove(Tree* tree, const char* path) {
  if (!is_path_valid(path)) return EINVAL;
  if (strcmp(path, "/") == 0) return EBUSY;


}