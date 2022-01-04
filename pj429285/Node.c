#include <stdlib.h>
#include <pthread.h>

#include "Node.h"
#include "path_utils.h"
#include "err.h"
#include "safe_malloc.h"

struct Node {
  HashMap* children;
  pthread_mutex_t lock;
  pthread_cond_t readers;
  pthread_cond_t writers;
  pthread_cond_t cleaner;
  int rcount, wcount, rwait, wwait, cwait;
  // r_to_let_in oznacza liczbę czytelników do wpuszczenia. Ustawia ją pierwszy
  // wpuszczony. Zachodzi r_to_let_in == -1, gdy nie trwa wpuszczanie.
  int r_to_let_in;
  // change == 2 -> wpuszczamy sprzątacza
  // change == 1 -> wpuszczamy czytelnika
  // change == 0 -> wpuszczamy pisarza
  // change == -1 -> nikogo nie wpuszczamy
  int change;

  bool to_delete;
};

Node* node_new() {
  Node* node = (Node *) safe_malloc(sizeof(Node));

  if ((node->children = hmap_new()) == NULL) exit(1);

  int err;

  if ((err = pthread_mutex_init(&node->lock, 0)) != 0)
    syserr(err, "mutex init failed");
  if ((err = pthread_cond_init(&node->readers, 0)) != 0)
    syserr (err, "cond init failed");
  if ((err = pthread_cond_init(&node->writers, 0)) != 0)
    syserr (err, "cond init failed");
  if ((err = pthread_cond_init(&node->cleaner, 0)) != 0)
    syserr (err, "cond init failed");

  node->rcount = 0;
  node->wcount = 0;
  node->rwait = 0;
  node->wwait = 0;
  node->cwait = 0;
  node->r_to_let_in = -1;
  node->change = 0;
  node->to_delete = false;
}

void node_free(Node* node) {
  hmap_free(node->children);

  int err;

  if ((err = pthread_cond_destroy (&node->readers)) != 0)
    syserr (err, "cond destroy failed");
  if ((err = pthread_cond_destroy (&node->writers)) != 0)
    syserr (err, "cond destroy failed");
  if ((err = pthread_cond_destroy (&node->cleaner)) != 0)
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

void node_set_to_delete(Node* node) {
  node->to_delete = true;
}

HashMap* node_get_children(Node* node) {
  return node->children;
}

int node_get_waiting_writers(Node* node) {
  return node->wwait;
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
    if (node->to_delete) {
      pthread_mutex_unlock(&node->lock);
      node_free(node);
      return;
    }

    node->r_to_let_in = -1;
    if (node->wwait > 0) {
      node->change = 0;
      if ((err = pthread_cond_signal(&node->writers)) != 0) {
        syserr(err, "cond signal failed");
      }
    }
    // Jeśli żaden pisarz nie czeka, to nie czekają też czytelnicy, więc
    // trzeba wpuścić sprzątacza, jeśli jakiś czeka.
    else if (node->cwait > 0) {
      node->change = 2;
      if ((err = pthread_cond_signal(&node->cleaner)) != 0) {
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
  else if (node->wwait > 0) {
    node->change = 0;
    if ((err = pthread_cond_signal(&node->writers)) != 0) {
      syserr(err, "cond signal failed");
    }
  }
  // Jeśli czeka tylko sprzątacz, to go wpuszczamy.
  else if (node->cwait > 0) {
    node->change = 2;
    if ((err = pthread_cond_signal(&node->cleaner)) != 0) {
      syserr(err, "cond signal failed");
    }
  }

  if ((err = pthread_mutex_unlock(&node->lock)) != 0)
    syserr (err, "unlock failed");
}

void start_cleaning(Node* node) {
  int err;
  if ((err = pthread_mutex_lock(&node->lock)) != 0)
    syserr (err, "lock failed");

  // Sprzątacz czeka.
  while (node->wcount + node->wwait + node->rcount + node->rwait > 0 && node->change != 2) {
    node->cwait++;
    if ((err = pthread_cond_wait(&node->cleaner, &node->lock)) != 0)
      syserr(err, "cond wait failed");
    node->cwait--;
  }

  node->change = -1;

  if ((err = pthread_mutex_unlock(&node->lock)) != 0)
    syserr (err, "unlock failed");
}


/*void start_reading(Node* node) {
  pthread_mutex_lock(&node->lock);

  if (node->wcount == 1 || node->wwait > 0) {
    // incrementing waiting readers
    node->rwait++;

    // reader suspended
    pthread_cond_wait(&node->readers, &node->lock);
    node->rwait--;
  }

  // else reader reads the resource
  node->rcount++;
  pthread_mutex_unlock(&node->lock);
  pthread_cond_broadcast(&node->readers);
}

void finish_reading(Node* node) {
  pthread_mutex_lock(&node->lock);

  if (--node->rcount == 0) {
    if (node->to_delete) {
      pthread_mutex_unlock(&node->lock);
      node_free(node);
      return;
    }

    if (node->wwait > 0) {
      pthread_cond_signal(&node->writers);
    }
    else {
      pthread_cond_signal(&node->cleaner);
    }
  }

  pthread_mutex_unlock(&node->lock);
}

void start_writing(Node* node) {
  pthread_mutex_lock(&node->lock);

  // a writer can enter when there are no active
  // or waiting readers or other writer
  if (node->wcount == 1 || node->rcount > 0) {
    ++node->wwait;
    pthread_cond_wait(&node->writers, &node->lock);
    --node->wwait;
  }
  node->wcount = 1;

  pthread_mutex_unlock(&node->lock);
}

void finish_writing(Node* node) {
  pthread_mutex_lock(&node->lock);
  node->wcount = 0;

  // if any readers are waiting, threads are unblocked
  if (node->rwait > 0)
    pthread_cond_signal(&node->readers);
  else if (node->wwait > 0)
    pthread_cond_signal(&node->writers);
  else
    pthread_cond_signal(&node->cleaner);
  pthread_mutex_unlock(&node->lock);
}

void start_cleaning(Node* node) {
  pthread_mutex_lock(&node->lock);

  if (node->wcount + node->wwait + node->rcount + node->rwait > 0) {
    pthread_cond_wait(&node->cleaner, &node->lock);
  }

  pthread_mutex_unlock(&node->lock);
}*/
