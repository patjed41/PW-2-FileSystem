// author - Patryk Jędrzejczak

/************************ DESCRIPTION OF THE SOLUTION *************************/

// The solution is to reduce the problem to the problem of readers and writers.
// Every node representing folder is a different reading room. Every thread
// is a reader or writer depending on what it wants to do. Every thread:
//  - wanting to reach node's child is a reader in this node.
//  - calling tree_create is a writer in the parent of the node to create.
//  - calling tree_list is a reader in the node to list.
//  - calling tree_remove is a writer in the parent of the node to remove and
//    a reader in the node to remove.
//  - calling tree_move is a writer in the source's parent and a writer in the
//    target's parent
// In addition, threads calling tree_remove and tree_move perform some special
// actions.
// Thread calling three_remove checks if node to remove has no children and
// no waiting writers - threads calling tree_create. If yes, it marks node as
// "to_delete" and removes it from its parent's HashMap. From this moment no
// threads will reach removed node, but readers in removed node can still work.
// The last finishing reader will notice "to_delete" mark and remove node's
// memory. This approach is correct, because the result of all function calls
// in removed node will always be equal to sequentially made calls with remove
// last.
// Thread calling tree_move reaches lca (lowest common ancestor) of source's
// parent and target's parent and it starts writing there. Then it reaches
// source's parent and target's parent and starts writing there. So thread
// calling tree_move is also a writer in lca(source's parent, target's parent).
// In my implementation it is necessary. Without this deadlocks could happen.
// For example, for tree with folders "/", "/a/", "/b/", "/a/c/", "/b/e/" two
// threads calling concurrently tree_move("/a/c/", "/b/f/") and
// tree_move("/b/e/", "/a/d/") could block themselves. Both threads could
// concurrently reach their source's parent and start writing there, but this
// way both target's parents would be locked by the other thread and never
// reached - deadlock. Even if this specific case can be solved, there are
// many similar problematic scenarios leading to deadlock. Fixing them all
// without locking at least part of the tree seems really difficult or even
// impossible. Locking lca(source's parent, target's parent) may not be
// the most optimal solution, but is general and easy to implement. That is
// why I chose this option.
// Moreover, thread calling tree_move waits until all operations in source's
// subtree finish. Without this, final effect of function calls could have no
// matching sequential substitute. For example, if tree_move finished
// before all tree_lists in source's subtree finished and tree_create
// reached source's subtree after tree_move finished, but before all tree_lists
// in the parent of node to create finished, final result would not match any
// sequential result. It is easy to check.

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "Tree.h"
#include "Node.h"
#include "path_utils.h"
#include "safe_alloc.h"

// Error returned by tree_move, when [source] is a prefix of [target].
#define EMOVETOSUBTREE -1

struct Tree {
  Node* root; // pointer to Node representing folder "/"
};

Tree* tree_new() {
  Tree* tree = (Tree *) safe_malloc(sizeof(Tree));

  tree->root = node_new();

  return tree;
}

void tree_free(Tree* tree) {
  node_recursive_free(tree->root);

  free(tree);
}

// Function finding Node which represents folder with path [path] in tree
// [tree]. If wanted Node does not exist, function returns NULL. Otherwise,
// function returns pointer to wanted Node in occupied state. If [as_reader]
// is equal to true, the Node is in reading state. If [as_reader] is equal to
// false, the Node is in writing state. Calling thread should finish
// reading/writing if wanted Node exists.
static Node* reach_node(Tree* tree, const char* path, bool as_reader) {
  char next_node_name[MAX_FOLDER_NAME_LENGTH + 1];
  const char* subpath = path;
  Node* current_node = tree->root;
  Node* next_node;

  if (strcmp(path, "/") == 0 && !as_reader) {
    start_writing(current_node);
  }
  else {
    start_reading(current_node);
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

    // Finishing reading in [current_node] after starting reading/writing
    // in [next_node] is necessary. Without this, for example, other thread
    // could remove [next_node] before calling thread started reading there.
    // Calling thread would end up with dangling pointer to [next_node].
    finish_reading(current_node);

    current_node = next_node;
  }

  return current_node;
}

// Similar function to reach_node(). This time searching starts in Node [start]
// which has to be in writing state by calling thread. Wanted Node, if exists,
// is always in writing state after function call.
static Node* reach_node_from(Node* start, const char* path) {
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
    else if (strcmp(subpath, "/") != 0) {
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

  Node* node = reach_node(tree, path, true);
  if (node == NULL) return NULL;

  char* result = make_map_contents_string(node_get_children(node));

  finish_reading(node);

  return result;
}

int tree_create(Tree* tree, const char* path) {
  if (!is_path_valid(path)) return EINVAL;
  if (strcmp(path, "/") == 0) return EEXIST;

  char node_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_parent = make_path_to_parent(path, node_name);
  Node* parent = reach_node(tree, path_to_parent, false);
  free(path_to_parent);

  if (parent == NULL) return ENOENT;

  if (hmap_get(node_get_children(parent), node_name) != NULL) {
    finish_writing(parent);
    return EEXIST;
  }
  else {
    Node* node = node_new();
    hmap_insert(node_get_children(parent), node_name, node);
    finish_writing(parent);
    return 0;
  }
}

int tree_remove(Tree* tree, const char* path) {
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

  Node* node = (Node*) hmap_get(node_get_children(parent), node_name);
  if (node == NULL) {
    finish_writing(parent);
    return ENOENT;
  }

  start_reading(node);
  if (hmap_size(node_get_children(node)) + node_get_waiting_writers(node) > 0) {
    finish_reading(node);
    finish_writing(parent);
    return ENOTEMPTY;
  }
  else {
    hmap_remove(node_get_children(parent), node_name);
    node_set_to_delete(node);
    finish_reading(node);
    finish_writing(parent);
    return 0;
  }
}

// Function forcing calling thread to wait until all operations in [node]'s
// subtree are finished. [node] has to be in writing state by calling thread.
static void finish_operations_in_subtree(Node* node) {
  start_cleaning(node);

  const char* child_name;
  void* child;
  HashMapIterator it = hmap_iterator(node_get_children(node));
  while (hmap_next(node_get_children(node), &it, &child_name, &child))
    finish_operations_in_subtree((Node*) child);
}

// Function freeing memory pointed by 3 pointers to char. It shortens tree_move.
static void free_three_strings(char* string1, char* string2, char* string3) {
  free(string1);
  free(string2);
  free(string3);
}

int tree_move(Tree* tree, const char* source, const char* target) {
  if (!is_path_valid(source) || !is_path_valid(target)) return EINVAL;
  if (strcmp(source, "/") == 0) return EBUSY;
  if (strcmp(target, "/") == 0) return EEXIST;
  if (strncmp(source, target, strlen(source)) == 0 && strcmp(source, target) != 0) return EMOVETOSUBTREE;

  char source_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_source_parent = make_path_to_parent(source, source_name);
  char target_name[MAX_FOLDER_NAME_LENGTH + 1];
  char* path_to_target_parent = make_path_to_parent(target, target_name);
  char* path_to_lca = make_path_to_lca(path_to_source_parent, path_to_target_parent);

  Node* lca = reach_node(tree, path_to_lca, false);
  if (lca == NULL) {
    free_three_strings(path_to_source_parent, path_to_target_parent, path_to_lca);
    return ENOENT;
  }

  Node* source_parent = reach_node_from(lca, path_to_source_parent + strlen(path_to_lca) - 1);
  if (source_parent == NULL) {
    finish_writing(lca);
    free_three_strings(path_to_source_parent, path_to_target_parent, path_to_lca);
    return ENOENT;
  }

  Node* target_parent = reach_node_from(lca, path_to_target_parent + strlen(path_to_lca) - 1);
  free_three_strings(path_to_source_parent, path_to_target_parent, path_to_lca);
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

    if (strcmp(source, target) == 0) return 0;
    return EEXIST;
  }

  finish_operations_in_subtree(source_node);

  hmap_insert(node_get_children(target_parent), target_name, source_node);
  hmap_remove(node_get_children(source_parent), source_name);

  finish_writing(lca);
  if (lca != source_parent) finish_writing(source_parent);
  if (lca != target_parent) finish_writing(target_parent);

  return 0;
}
