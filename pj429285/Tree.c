#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "Tree.h"
#include "Node.h"
#include "path_utils.h"
#include "safe_malloc.h"
#include "err.h"

#include <stdio.h> // DO USUNIECIA
#include <pthread.h>

// Błąd zwracany przez move, gdy source jest prefiksem target.
#define EMOVETOSUBTREE -1

struct Tree {
  Node* root;

  pthread_mutex_t move_lock;
};

Tree* tree_new() {
  Tree* tree = (Tree *) safe_malloc(sizeof(Tree));

  tree->root = node_new();

  int err;

  if ((err = pthread_mutex_init(&tree->move_lock, 0)) != 0)
    syserr(err, "mutex init failed");

  return tree;
}

void node_print(Node* node, char name[], int length) {
  for (int i = 0; i < length; i++)
    printf("%c", name[i]);
  printf("\n");

  const char* key;
  void* value;
  HashMapIterator it = hmap_iterator(node_get_children(node));
  while (hmap_next(node_get_children(node), &it, &key, &value)) {
    for (int j = 0; j < strlen(key); j++)
      name[length + j] = key[j];
    name[length + strlen(key) + 1] = '/';
    node_print((Node *) value, name, length + strlen(key) + 2);
  }
}

void tree_print(Tree* tree) {
  char name[MAX_PATH_LENGTH + 1];
  name[0] = '/';
  int length = 1;
  node_print(tree->root, name, length);
}

void tree_free(Tree* tree) {
  node_recursive_free(tree->root);

  int err;

  if ((err = pthread_mutex_destroy (&tree->move_lock)) != 0)
    syserr (err, "mutex destroy failed");

  free(tree);
}

static Node* reach_node(Tree* tree, const char* path, bool as_reader) {
  //printf("path: %s\n", path);
  char next_node_name[MAX_FOLDER_NAME_LENGTH + 1];
  const char* subpath = path;
  Node* current_node = tree->root;
  Node* next_node;

  if (strcmp(path, "/") == 0 && !as_reader) {
    start_writing(current_node);
    //printf("Create w korzeniu:\n");
    //print_state(current_node);
  }
  else {
    start_reading(current_node);
    //printf("List w korzeniu:\n");
    //print_state(current_node);
  }

  while ((subpath = split_path(subpath, next_node_name)) != NULL) {
    next_node = hmap_get(node_get_children(current_node), next_node_name);
    if (next_node == NULL) {
      finish_reading(current_node);
      return NULL;
    }
    else if (strcmp(subpath, "/") != 0 || as_reader){
      start_reading(next_node);
    }
    else {
      start_writing(next_node);
    }

    //printf("List zajął /a/\n");
    //print_state(current_node);
    //print_state(next_node);

    finish_reading(current_node);

    current_node = next_node;
  }

  return current_node;
}

static Node* reach_node_from(Node* start, const char* path, bool as_reader) {
  char next_node_name[MAX_FOLDER_NAME_LENGTH + 1];
  const char* subpath = path;
  Node* current_node = start;
  Node* next_node;

  while ((subpath = split_path(subpath, next_node_name)) != NULL) {
    next_node = hmap_get(node_get_children(current_node), next_node_name);
    if (next_node == NULL) {
      if (current_node != start) finish_reading(current_node);
      return NULL;
    }
    else if (strcmp(subpath, "/") != 0 || as_reader){
      start_reading(next_node);
    }
    else {
      start_writing(next_node);
    }

    if (current_node != start) finish_reading(current_node);

    current_node = next_node;
  }

  return current_node;
}

char* tree_list(Tree* tree, const char* path) {
  if (!is_path_valid(path)) return NULL;

  //printf("start: tree_list(tree, %s);\n", path);

  Node* node = reach_node(tree, path, true);
  if (node == NULL) return NULL;

  char* result = make_map_contents_string(node_get_children(node));

  finish_reading(node);

  //printf("end: tree_list(tree, %s);\n", path);

  return result;
}

int tree_create(Tree* tree, const char* path) {
  if (!is_path_valid(path)) return EINVAL;
  if (strcmp(path, "/") == 0) return EEXIST;

  //printf("start: tree_create(tree, %s);\n", path);
  //tree_print(tree);

  char node_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_parent = make_path_to_parent(path, node_name);

  //printf("path: %s, path_to_parent: %s\n", path, path_to_parent);
  Node* parent = reach_node(tree, path_to_parent, false);
  free(path_to_parent);

  if (parent == NULL) {
    //printf("end: tree_create(tree, %s);\n", path);
    return ENOENT;
  }

  int result = 0;

  if (hmap_get(node_get_children(parent), node_name) != NULL) {
    result = EEXIST;
  }
  else {
    Node* node = node_new();
    hmap_insert(node_get_children(parent), node_name, node);
  }

  //printf("path: %s\n", path);
  //print_state(parent);
  //printf("r:\n");
  //print_state(tree->root);

  finish_writing(parent);
  //printf("end: tree_create(tree, %s);\n", path);

  return result;
}

int tree_remove(Tree* tree, const char* path) {
  //printf("start: tree_remove(tree, %s);\n", path);
  if (!is_path_valid(path)) return EINVAL;
  if (strcmp(path, "/") == 0) return EBUSY;

  char node_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_parent = make_path_to_parent(path, node_name);
  Node* parent = reach_node(tree, path_to_parent, false);
  free(path_to_parent);
  if (parent == NULL) return ENOENT;

  if (hmap_get(node_get_children(parent), node_name) == NULL) {
    finish_writing(parent);
    return ENOENT;
  }

  int result = 0;

  Node* node = (Node*) hmap_get(node_get_children(parent), node_name);
  if (node == NULL) {
    result = ENOENT;
  }
  else {
    start_reading(node);
    if (hmap_size(node_get_children(node)) + node_get_waiting_writers(node) > 0) {
      result = ENOTEMPTY;
    }
    else {
      hmap_remove(node_get_children(parent), node_name);
      node_set_to_delete(node);
    }
    finish_reading(node);
  }

  finish_writing(parent);

  //printf("end: tree_remove(tree, %s);\n", path);

  return result;
}

static void finish_operations_in_subtree(Node* node) {
  start_cleaning(node);

  const char* child_name;
  void* child;
  HashMapIterator it = hmap_iterator(node_get_children(node));
  while (hmap_next(node_get_children(node), &it, &child_name, &child))
    finish_operations_in_subtree((Node*) child);
}

/*int tree_move(Tree* tree, const char* source, const char* target) {
  //tree_print(tree);

  if (!is_path_valid(source) || !is_path_valid(target)) return EINVAL;
  if (strcmp(source, "/") == 0) return EBUSY;
  if (strcmp(target, "/") == 0) return EEXIST;
  if (strncmp(source, target, strlen(source)) == 0 && strcmp(source, target) != 0) return EMOVETOSUBTREE;

  int err;

  if ((err = pthread_mutex_lock(&tree->move_lock)) != 0) syserr(err, "lock failed");

  //printf("start: tree_move(tree, %s, %s);\n", source, target);

  char target_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_target_parent = make_path_to_parent(target, target_name);
  Node* target_parent = reach_node(tree, path_to_target_parent, false);
  if (target_parent == NULL) {
    free(path_to_target_parent);
    //printf("end: tree_move(tree, %s, %s);\n", source, target);
    if ((err = pthread_mutex_unlock(&tree->move_lock)) != 0) syserr(err, "unlock failed");
    return ENOENT;
  }
  else if (hmap_get(node_get_children(target_parent), target_name) != NULL) {
    free(path_to_target_parent);
    finish_writing(target_parent);
    //printf("end: tree_move(tree, %s, %s);\n", source, target);
    if ((err = pthread_mutex_unlock(&tree->move_lock)) != 0) syserr(err, "unlock failed");
    return EEXIST;
  }

  char source_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_source_parent = make_path_to_parent(source, source_name);
  Node* source_parent;
  if (strcmp(path_to_source_parent, path_to_target_parent) == 0) {
    source_parent = target_parent;
  }
  else if (strncmp(path_to_source_parent, path_to_target_parent, strlen(path_to_target_parent)) == 0) {
    source_parent = reach_node_from(target_parent, path_to_source_parent + strlen(path_to_target_parent) - 1, false);
  }
  else {
    source_parent = reach_node(tree, path_to_source_parent, false);
  }
  free(path_to_target_parent);
  free(path_to_source_parent);

  if ((err = pthread_mutex_unlock(&tree->move_lock)) != 0) syserr(err, "unlock failed");

  if (source_parent == NULL) {
    finish_writing(target_parent);
    //printf("end: tree_move(tree, %s, %s);\n", source, target);
    return ENOENT;
  }

  Node* source_node = (Node*) hmap_get(node_get_children(source_parent), source_name);
  if (source_node == NULL) {
    finish_writing(target_parent);
    if (target_parent != source_parent) finish_writing(source_parent);
    //printf("end: tree_move(tree, %s, %s);\n", source, target);
    return ENOENT;
  }

  finish_operations_in_subtree(source_node);
  hmap_insert(node_get_children(target_parent), target_name, source_node);
  hmap_remove(node_get_children(source_parent), source_name);

  finish_writing(target_parent);
  if (target_parent != source_parent) finish_writing(source_parent);

  //printf("end: tree_move(tree, %s, %s);\n", source, target);

  return 0;
}*/

int tree_move(Tree* tree, const char* source, const char* target) {
  if (!is_path_valid(source) || !is_path_valid(target)) return EINVAL;
  if (strcmp(source, "/") == 0) return EBUSY;
  if (strcmp(target, "/") == 0) return EEXIST;
  if (strncmp(source, target, strlen(source)) == 0 && strcmp(source, target) != 0) return EMOVETOSUBTREE;

  //printf("start: tree_move(tree, %s, %s);\n", source, target);

  char source_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_source_parent = make_path_to_parent(source, source_name);
  char target_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_target_parent = make_path_to_parent(target, target_name);
  char* path_to_lca = make_path_to_lca(path_to_source_parent, path_to_target_parent);

  Node* lca = reach_node(tree, path_to_lca, false);
  if (lca == NULL) {
    free(path_to_source_parent);
    free(path_to_target_parent);
    free(path_to_lca);
    return ENOENT;
  }

  Node* source_parent = reach_node_from(lca, path_to_source_parent + strlen(path_to_lca) - 1, false);
  free(path_to_source_parent);
  if (source_parent == NULL) {
    finish_writing(lca);
    free(path_to_target_parent);
    free(path_to_lca);
    return ENOENT;
  }

  Node* target_parent = reach_node_from(lca, path_to_target_parent + strlen(path_to_lca) - 1, false);
  free(path_to_target_parent);
  free(path_to_lca);
  if (target_parent == NULL) {
    finish_writing(lca);
    if (lca != source_parent) finish_writing(source_parent);
    return ENOENT;
  }

  Node* source_node = (Node*) hmap_get(node_get_children(source_parent), source_name);

  if (source_node == NULL) {
    finish_writing(lca);
    if (lca != source_parent) finish_writing(source_parent);
    if (lca != target_parent) finish_writing(target_parent);
    return ENOENT;
  }

  if (hmap_get(node_get_children(target_parent), target_name) != NULL) {
    finish_writing(lca);
    if (lca != source_parent) finish_writing(source_parent);
    if (lca != target_parent) finish_writing(target_parent);
    return EEXIST;
  }

  finish_operations_in_subtree(source_node);
  hmap_insert(node_get_children(target_parent), target_name, source_node);
  hmap_remove(node_get_children(source_parent), source_name);

  finish_writing(lca);
  if (lca != source_parent) finish_writing(source_parent);
  if (lca != target_parent) finish_writing(target_parent);

  //printf("end: tree_move(tree, %s, %s);\n", source, target);

  return 0;
}

/*static void finish_all_operations(Tree* tree, Node* node) {
  if (node != tree->root)
    start_cleaning(node);

  const char* child_name;
  void* child;
  HashMapIterator it = hmap_iterator(node_get_children(node));
  while (hmap_next(node_get_children(node), &it, &child_name, &child))
    finish_all_operations(tree, (Node*) child);
}

static Node* reach_node_safe(Tree* tree, const char* path) {
  char next_node_name[MAX_FOLDER_NAME_LENGTH + 1];
  const char* subpath = path;
  Node* current_node = tree->root;
  Node* next_node;

  while ((subpath = split_path(subpath, next_node_name)) != NULL) {
    next_node = hmap_get(node_get_children(current_node), next_node_name);
    if (next_node == NULL) {
      return NULL;
    }
    current_node = next_node;
  }

  return current_node;
}

int tree_move(Tree* tree, const char* source, const char* target) {
  //printf("start: tree_move(tree, %s, %s);\n", source, target);
  //tree_print(tree);

  if (!is_path_valid(source) || !is_path_valid(target)) return EINVAL;
  if (strcmp(source, "/") == 0) return EBUSY;
  if (strcmp(target, "/") == 0) return EEXIST;
  if (strncmp(source, target, strlen(source)) == 0 && strcmp(source, target) != 0) return EMOVETOSUBTREE;

  start_writing(tree->root);
  finish_all_operations(tree, tree->root);

  char target_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_target_parent = make_path_to_parent(target, target_name);
  Node* target_parent = reach_node_safe(tree, path_to_target_parent);
  free(path_to_target_parent);
  if (target_parent == NULL) {
    finish_writing(tree->root);
    return ENOENT;
  }
  else if (hmap_get(node_get_children(target_parent), target_name) != NULL) {
    finish_writing(tree->root);
    return EEXIST;
  }

  char source_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_source_parent = make_path_to_parent(source, source_name);
  Node* source_parent = reach_node_safe(tree, path_to_source_parent);
  free(path_to_source_parent);
  if (source_parent == NULL) {
    finish_writing(tree->root);
    return ENOENT;
  }

  Node* source_node = (Node*) hmap_get(node_get_children(source_parent), source_name);
  if (source_node == NULL) {
    finish_writing(tree->root);
    return ENOENT;
  }

  hmap_insert(node_get_children(target_parent), target_name, source_node);
  hmap_remove(node_get_children(source_parent), source_name);

  finish_writing(tree->root);

  //printf("end: tree_move(tree, %s, %s);\n", source, target);

  return 0;
}*/