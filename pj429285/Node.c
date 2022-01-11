#include <stdlib.h>
#include <pthread.h>

#include "Node.h"
#include "err.h"
#include "safe_alloc.h"

// Every Node is a reading room. Not only can typical readers and writers
// enter the reading room, but also cleaners. Cleaners can enter only if
// no one is in the reading room and no one wants to enter reading room.
// Only one cleaner can enter reading room and only one can wait. Otherwise,
// behaviour of reading room is undefined. Cleaners call start_cleaning before
// cleaning. Thread calling tree_move is a cleaner in every node of source's
// subtree and calls start_cleaning in every node of source's subtree to wait
// for finish of all operations.
struct Node {
  HashMap* children;      // HashMap containing pointers to children
  pthread_mutex_t lock;   // mutex needed to implement monitor
  pthread_cond_t readers; // readers are waiting here
  pthread_cond_t writers; // writers are waiting here
  pthread_cond_t cleaner; // cleaner is waiting here (only one can)
  int rcount;             // number of reading readers
  int wcount;             // number of writing writers
  int rwait;              // number of waiting readers
  int wwait;              // number of waiting writers
  int cwait;              // number of waiting cleaners
  int r_to_let_in;        // number of readers to let in
  // change == 2 -> we let cleaner in
  // change == 1 -> we let reader in
  // change == 0 -> we let writer in
  // change == -1 -> no one can enter
  int change;
  bool to_delete;         // equals to true if node should be freed
};

Node* node_new() {
  Node* node = (Node *) safe_malloc(sizeof(Node));

  if ((node->children = hmap_new()) == NULL) exit(1);

  if (pthread_mutex_init(&node->lock, 0) != 0)
    fatal("mutex init failed");
  if (pthread_cond_init(&node->readers, 0) != 0)
    fatal("cond init failed");
  if (pthread_cond_init(&node->writers, 0) != 0)
    fatal("cond init failed");
  if (pthread_cond_init(&node->cleaner, 0) != 0)
    fatal("cond init failed");

  node->rcount = 0;
  node->wcount = 0;
  node->rwait = 0;
  node->wwait = 0;
  node->cwait = 0;
  node->r_to_let_in = -1;
  node->change = 0;
  node->to_delete = false;

  return node;
}

void node_free(Node* node) {
  hmap_free(node->children);

  if (pthread_cond_destroy (&node->readers) != 0)
    fatal("cond destroy failed");
  if (pthread_cond_destroy (&node->writers) != 0)
    fatal("cond destroy failed");
  if (pthread_cond_destroy (&node->cleaner) != 0)
    fatal("cond destroy failed");
  if (pthread_mutex_destroy (&node->lock) != 0)
    fatal("mutex destroy failed");

  free(node);
}

void node_recursive_free(Node* node) {
  const char* child_name;
  void* child;
  HashMapIterator it = hmap_iterator(node->children);
  while (hmap_next(node->children, &it, &child_name, &child)) {
    node_recursive_free((Node*) child);
  }

  node_free(node);
}

void node_set_to_delete(Node* node) {
  node->to_delete = true;
}

HashMap* node_get_children(Node* node) {
  return node->children;
}

int node_get_waiting_writers(Node* node) {
  return node->wwait;
}

static void let_readers_in(Node* node) {
  node->change = 1;
  if (pthread_cond_signal(&node->readers) != 0)
    fatal("cond signal failed");
}

static void let_writer_in(Node* node) {
  node->change = 0;
  if (pthread_cond_signal(&node->writers) != 0)
    fatal("cond signal failed");
}

static void let_cleaner_in(Node* node) {
  node->change = 2;
  if (pthread_cond_signal(&node->cleaner) != 0)
    fatal("cond signal failed");
}

void start_reading(Node* node) {
  if (pthread_mutex_lock(&node->lock) != 0)
    fatal("lock failed");

  // Reader is waiting.
  while (node->wcount + node->wwait > 0 && node->change != 1) {
    node->rwait++;
    if (pthread_cond_wait(&node->readers, &node->lock) != 0)
      fatal("cond wait failed");
    node->rwait--;
  }

  node->rcount++;

  // We let another reader in if we can.
  if (node->rwait > 0 && node->r_to_let_in != 0) {
    if (node->r_to_let_in == -1) {
      node->r_to_let_in = node->rwait;
    }
    node->r_to_let_in--;
    node->change = 1;
    if (pthread_cond_signal(&node->readers) != 0)
      fatal("cond signal failed");
  }
  else {
    node->change = -1;
  }

  if (pthread_mutex_unlock(&node->lock) != 0)
    fatal("unlock failed");
}

void finish_reading(Node* node) {
  if (pthread_mutex_lock(&node->lock) != 0)
    fatal("lock failed");

  node->rcount--;

  // Last finishing reader decides what to do next.
  if (node->rcount == 0) {
    node->r_to_let_in = -1;

    // Last reader frees node if it should be freed.
    if (node->to_delete) {
      if (node->rwait > 0) {
        let_readers_in(node);
        if (pthread_mutex_unlock(&node->lock) != 0)
          fatal("unlock failed");
      }
      else {
        if (pthread_mutex_unlock(&node->lock) != 0)
          fatal("unlock failed");
        node_free(node);
      }
      return;
    }

    // Last reader lets a writer in if at least one writer is waiting.
    if (node->wwait > 0)
      let_writer_in(node);
    // Otherwise, last reader lets reader in if at least one reader is waiting.
    else if (node->rwait > 0)
      let_readers_in(node);
    // If no one is waiting and cleaner is waiting, last reader lets cleaner in.
    else if (node->cwait > 0)
      let_cleaner_in(node);
  }

  if (pthread_mutex_unlock(&node->lock) != 0)
    fatal("unlock failed");
}

void start_writing(Node* node) {
  if (pthread_mutex_lock(&node->lock) != 0)
    fatal("lock failed");

  // Writer is waiting.
  while (node->wcount + node->rcount + node->rwait > 0 && node->change != 0) {
    node->wwait++;
    if (pthread_cond_wait(&node->writers, &node->lock) != 0)
      fatal("cond wait failed");
    node->wwait--;
  }

  node->change = -1;
  node->wcount++;

  if (pthread_mutex_unlock(&node->lock) != 0)
    fatal("unlock failed");
}

void finish_writing(Node* node) {
  if (pthread_mutex_lock(&node->lock) != 0)
    fatal("lock failed");

  node->wcount--;

  // Writer lets reader in if at least one is waiting.
  if (node->rwait > 0)
    let_readers_in(node);
  // Otherwise, writer lets writer in if at least one is waiting.
  else if (node->wwait > 0)
    let_writer_in(node);
  // If no one is waiting and cleaner is waiting, writer lets cleaner in.
  else if (node->cwait > 0)
    let_cleaner_in(node);

  if (pthread_mutex_unlock(&node->lock) != 0)
    fatal("unlock failed");
}

void start_cleaning(Node* node) {
  if (pthread_mutex_lock(&node->lock) != 0)
    fatal("lock failed");

  // Cleaner is waiting.
  while (node->wcount + node->wwait + node->rcount + node->rwait > 0 && node->change != 2) {
    node->cwait++;
    if (pthread_cond_wait(&node->cleaner, &node->lock) != 0)
      fatal("cond wait failed");
    node->cwait--;
  }

  node->change = -1;

  if (pthread_mutex_unlock(&node->lock) != 0)
    fatal("unlock failed");
}
