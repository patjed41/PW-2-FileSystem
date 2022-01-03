#include <stdlib.h>
#include <pthread.h>

#include "Node.h"
#include "HashMap.h"
#include "err.h"
#include "safe_malloc.h"

struct Node {
  const char* name;
  HashMap* children;
  pthread_mutex_t lock;
  pthread_cond_t readers;
  pthread_cond_t writers;
  int rcount, wcount, rwait, wwait;
  // r_to_let_in oznacza liczbę czytelników do wpuszczenia. Ustawia ją pierwszy
  // wpuszczony. Zachodzi r_to_let_in == -1, gdy nie trwa wpuszczanie.
  int r_to_let_in;
  // change == 1 -> wpuszczamy czytelnika
  // change == 0 -> wpuszczamy pisarza
  // change == -1 -> nikogo nie wpuszczamy
  int change;
};

Node* node_new(const char* name) {
  Node* node = (Node *) safe_malloc(sizeof(Node));

  node->name = name;

  if ((node->children = hmap_new()) == NULL) exit(1);

  int err;

  if ((err = pthread_mutex_init(&node->lock, 0)) != 0)
    syserr(err, "mutex init failed");
  if ((err = pthread_cond_init(&node->readers, 0)) != 0)
    syserr (err, "cond init failed");
  if ((err = pthread_cond_init(&node->writers, 0)) != 0)
    syserr (err, "cond init failed");

  node->rcount = 0;
  node->wcount = 0;
  node->rwait = 0;
  node->wwait = 0;
  node->r_to_let_in = -1;
  node->change = 0;
}

void node_free(Node* node) {
  hmap_free(node->children);

  int err;

  if ((err = pthread_cond_destroy (&node->readers)) != 0)
    syserr (err, "cond destroy failed");
  if ((err = pthread_cond_destroy (&node->writers)) != 0)
    syserr (err, "cond destroy failed");
  if ((err = pthread_mutex_destroy (&node->lock)) != 0)
    syserr (err, "mutex destroy failed");

  free(node);
}

void node_recursive_free(Node* node) {
  const char* child_name;
  void* child;
  HashMapIterator it = hmap_iterator(node->children);
  while (hmap_next(node->children, &it, &child_name, &child))
    node_recursive_free((Node*) child);

  node_free(node);
}

void start_reading(Node* node) {
  int err;
  if ((err = pthread_mutex_lock(&node->lock)) != 0)
    syserr (err, "lock failed");

  // Czytelnik czeka.
  while (node->wcount + node->wwait > 0 && node->change != 1) {
    node->rwait++;
    if ((err = pthread_cond_wait(&node->readers, &node->lock)) != 0)
      syserr (err, "cond wait failed");
    node->rwait--;
  }

  node->rcount++;

  // Jeśli jest kogo wpuścić i nie skończyło się wpuszczanie, to wpuszczamy.
  if (node->rwait > 0 && node->r_to_let_in != 0) {
    // Pierwszy wpuszczony ustawia liczbę czytelników do wpuszczenia.
    if (node->r_to_let_in == -1) {
      node->r_to_let_in = node->rwait;
    }
    node->r_to_let_in--;
    node->change = 1;
    if ((err = pthread_cond_signal(&node->readers)) != 0)
      syserr (err, "cond signal failed");
  }
  else {
    node->change = -1;
  }

  if ((err = pthread_mutex_unlock(&node->lock)) != 0)
    syserr (err, "unlock failed");
}

void finish_reading(Node* node) {
  int err;
  if ((err = pthread_mutex_lock(&node->lock)) != 0)
    syserr (err, "lock failed");

  node->rcount--;

  // Ostatni musi ustawić rw->r_to_let_in na -1 i wpuscić pisarza, jeśli czeka.
  if (node->rcount == 0) {
    node->r_to_let_in = -1;
    if (node->wwait > 0) {
      node->change = 0;
      if ((err = pthread_cond_signal(&node->writers)) != 0) {
        syserr(err, "cond signal failed");
      }
    }
  }

  if ((err = pthread_mutex_unlock(&node->lock)) != 0)
    syserr (err, "unlock failed");
}

void start_writing(Node* node) {
  int err;
  if ((err = pthread_mutex_lock(&node->lock)) != 0)
    syserr (err, "lock failed");

  // Pisarz czeka.
  while (node->wcount + node->rcount + node->rwait > 0 && node->change != 0) {
    node->wwait++;
    if ((err = pthread_cond_wait(&node->writers, &node->lock)) != 0)
      syserr(err, "cond wait failed");
    node->wwait--;
  }

  node->change = -1;
  node->wcount++;

  if ((err = pthread_mutex_unlock(&node->lock)) != 0)
    syserr (err, "unlock failed");
}

void finish_writing(Node* node) {
  int err;
  if ((err = pthread_mutex_lock(&node->lock)) != 0)
    syserr (err, "lock failed");

  node->wcount--;

  // Pisarz wpuszcza czytelnika, jeśli jakiś czeka.
  if (node->rwait > 0) {
    node->change = 1;
    if ((err = pthread_cond_signal(&node->readers)) != 0) {
      syserr(err, "cond signal failed");
    }
  }
  // Jeśli czytelnicy nie czekają, pisarz wpuszcza pisarza, jeśli jakiś czeka.
  else if (node->wwait) {
    node->change = 0;
    if ((err = pthread_cond_signal(&node->writers)) != 0) {
      syserr(err, "cond signal failed");
    }
  }

  if ((err = pthread_mutex_unlock(&node->lock)) != 0)
    syserr (err, "unlock failed");
}
