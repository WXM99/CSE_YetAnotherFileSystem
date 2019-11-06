// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

// #define debug

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);
  pthread_mutex_init(&threads_mutex, NULL);
}

void
lock_client_cache::xlock(lock_protocol::lockid_t lid, const char* action) {
  static int count = 0;
  printf("=====xlock:%d-%s-%lld=====\n",count, action, lid);
  count++;
  cached_lock_p lock = lock_cache[lid];
  if (lock == NULL) {
    printf("no cache found in client\n");
    printf("========end========\n");
    return;
  }
  const char* states[5]={"none","free","locked", "acquring", "releasing"};
  const char* resps[4]={"empty","revoke","retry", "RETRY"};
  printf("lock_state: %s\n", states[lock->client_state]);
  printf("server_response: %s\n", resps[lock->server_response]);
  int size = lock->threads_queue.size();
  printf("queue size: %d\n", size);
  for (int i = 0; i < size; i ++) {
    pthread_cond_t* cond = lock->threads_queue[i];
    printf("queue[%d]: %p\n", i, (void*)cond);
  }
  printf("========end========\n");
  return;
}

/* 
 * rpc_acquire: 
 * when it is needed to send a substantial PRC 
 * acquire call to the server by the client,
 * that is, the lock in client is "none".
 */
lock_protocol::status
lock_client_cache::rpc_acquire(lock_protocol::lockid_t lid, 
        cached_lock_p lock, pthread_cond_t* thread)
{
  // change state
  lock->client_state = acquiring;
  // try to acquire
  while (lock->client_state == acquiring)
  {
    pthread_mutex_unlock(&threads_mutex);
    /* substantial acquire from server */
    int r;
    int ret = cl->call(lock_protocol::acquire, lid, lock_client_cache::id, r);
    pthread_mutex_lock(&threads_mutex);
    // if got the lock from server
    if (ret == lock_protocol::OK) {
      lock->client_state = locked;
      pthread_mutex_unlock(&threads_mutex);
      return lock_protocol::OK;
    }
    // if server return RETRY (CAPITAL means acquire is not accepted)
    else {
      // if server did not say retry (lower case means try again), just wait
      if (lock->server_response != retry) {
        pthread_cond_wait(thread, &threads_mutex);
      }
      // if server saied retry, clear response and then acquire
      lock->server_response = empty;
    }
  }
  return lock_protocol::NOENT;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  pthread_mutex_lock(&threads_mutex);
  cached_lock_p lock = lock_cache[lid];
  if (lock == NULL) {
    lock = (cached_lock_p) new client_cached_lock();
    lock_cache[lid] = lock;
  }
  pthread_cond_t* cond = new pthread_cond_t();
  pthread_cond_init(cond, NULL);
  #ifdef debug
  xlock(lid, "acq");
  #endif
  if (lock->threads_queue.empty()) {
    lock->threads_queue.push_back(cond);
    switch (lock->client_state) {
      case none: {
        return rpc_acquire(lid, lock, cond);
      }
      case free: {
        lock->client_state = locked;
        pthread_mutex_unlock(&threads_mutex);
        return ret;
      }
      case releasing: {
        // todo: should have called rpc_acquire() but to reduce calling
        pthread_cond_wait(cond, &threads_mutex);
        return rpc_acquire(lid, lock, cond);
        // pthread_mutex_unlock(&threads_mutex);
        // return ret;
      }
      default: {
        return lock_protocol::NOENT;
      }
    }
  } else {
    lock->threads_queue.push_back(cond);
    pthread_cond_wait(cond, &threads_mutex);
    switch (lock->client_state) {
      case none: {
        return rpc_acquire(lid, lock, cond);
      }
      case free: 
      case locked: {
        lock->client_state = locked;
        pthread_mutex_unlock(&threads_mutex);
        return ret;
      }
      default: {
        return lock_protocol::NOENT;
      }
    }
  }
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  int r;
  pthread_mutex_lock(&threads_mutex);
  cached_lock_p lock = lock_cache[lid];
  bool still_cached = true;
  #ifdef debug 
  xlock(lid, "rel");
  #endif
  // release lock to the server if it is a revoked lock
  if (lock->server_response == revoke && lock->threads_queue.size() <= 1) {
    still_cached = false;
    lock->client_state = releasing;
    pthread_mutex_unlock(&threads_mutex);
    /* substantial release */
    ret = cl->call(lock_protocol::release, lid, id, r);
    pthread_mutex_lock(&threads_mutex);
    lock->client_state = none;
    lock->server_response = empty;
  }
  // release but cached if not revoked 
  else {
    lock->client_state = free;
  }
  // pop and check threads queue
  delete lock->threads_queue.front();
  lock->threads_queue.pop_front();
  // schedule to next thread in the queue if it has
  if (!lock->threads_queue.empty()) {
    if (still_cached) lock->client_state = locked;
    pthread_cond_signal(lock->threads_queue.front());
  }
  pthread_mutex_unlock(&threads_mutex);
  return ret;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;
  int r;
  pthread_mutex_lock(&threads_mutex);
  cached_lock_p lock = lock_cache[lid];
  // lock is free, then release to server
  if (lock->client_state == free && lock->threads_queue.size() <= 1) {
    lock->client_state = releasing;
    pthread_mutex_unlock(&threads_mutex);
    /* substantial release */
    ret = cl->call(lock_protocol::release, lid, id, r);
    pthread_mutex_lock(&threads_mutex);
    // schedule to next thread in the queue if it has
    lock->client_state = none;
    if (!lock->threads_queue.empty()) {
      pthread_cond_signal(lock->threads_queue.front());
    }
  }
  // lock in cache is not free, set the response as revoke
  else {
    lock->server_response = revoke; 
  }
  pthread_mutex_unlock(&threads_mutex);
  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
  pthread_mutex_lock(&threads_mutex);
  cached_lock_p lock = lock_cache[lid];
  // set response and schedule to next thread
  lock->server_response = retry;
  if (!lock->threads_queue.empty()) {
    pthread_cond_signal(lock->threads_queue.front());
  }
  pthread_mutex_unlock(&threads_mutex);
  return ret;
}



