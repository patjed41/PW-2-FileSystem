#include <errno.h>
#include <stdlib.h>

#include "Tree.h"
#include "Node.h"
#include "safe_malloc.h"

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