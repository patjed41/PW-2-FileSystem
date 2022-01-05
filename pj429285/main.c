#include "HashMap.h"
#include "Tree.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "path_utils.h"

void print_map(HashMap* map) {
    const char* key = NULL;
    void* value = NULL;
    printf("Size=%zd\n", hmap_size(map));
    HashMapIterator it = hmap_iterator(map);
    while (hmap_next(map, &it, &key, &value)) {
        printf("Key=%s Value=%p\n", key, value);
    }
    printf("\n");
}


int main(void) {
  HashMap* map = hmap_new();
  hmap_insert(map, "a", hmap_new());
  print_map(map);

  HashMap* child = (HashMap*)hmap_get(map, "a");
  hmap_free(child);
  hmap_remove(map, "a");
  print_map(map);

  hmap_free(map);

  //return 0;
  Tree* t = tree_new();

  printf("%s\n", make_path_to_lca("/a/b/r/", "/a/b/c/d/e/"));

  /*tree_create(t, "/a/");
  tree_create(t, "/b/");
  printf("%s\n", tree_list(t, "/"));
  tree_create(t, "/a/aa/");
  tree_create(t, "/a/bb/");
  printf("%s\n", tree_list(t, "/a/"));
  tree_create(t, "/a/aa/aaa/");
  tree_create(t, "/a/aa/bbb/");
  printf("%s\n", tree_list(t, "/a/aa/"));
  tree_remove(t, "/a/aa/aaa/");
  printf("%s\n", tree_list(t, "/a/aa/"));
  tree_move(t, "/a/aa/", "/a/cc/");
  tree_print(t);*/



  /*tree_create(t, "/a/");
  tree_create(t, "/a/b/");
  tree_create(t, "/a/b/c/");
  tree_create(t, "/a/b/c/d/");
  tree_move(t, "/a/b/c/", "/a/x/");
  printf("%s\n", tree_list(t, "/a/"));*/

  tree_free(t);
}