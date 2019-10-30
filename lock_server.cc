// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
  pthread_mutex_init(&mutex, NULL);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
	// Your lab2 part2 code goes here
  pthread_mutex_lock(&mutex);

  if (locked.find(lid) == locked.end()) {
    pthread_cond_t *init = new pthread_cond_t;
    pthread_cond_init(init, NULL);
    conditions[lid] = init;
    lock(lid);
    goto release;
  }

  if (!locked[lid]) {
    lock(lid);
    goto release;
  }

  while (locked[lid]) {
    pthread_cond_wait(conditions[lid], &mutex);
  }
  lock(lid);

  release:
  r = ret = pthread_mutex_unlock(&mutex);
  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
	// Your lab2 part2 code goes here
  pthread_mutex_lock(&mutex);
  unlock(lid);
  pthread_cond_signal(conditions[lid]);
  r = ret = pthread_mutex_unlock(&mutex);
  return ret;
}

void
lock_server::lock(lock_protocol::lockid_t lid)
{
  if (locked[lid]) {
    printf("lock_server: error: lock a locked at %lld\n", lid);
  }
  printf("lock_server: lock %lld\n", lid);
  locked[lid] = true;
}

void
lock_server::unlock(lock_protocol::lockid_t lid)
{
  if (!locked[lid]) {
    printf("lock_server: error: unlock a free lock %lld\n", lid);
  }
  printf("lock_server: unlock %lld\n", lid);
  locked[lid] = false;
}