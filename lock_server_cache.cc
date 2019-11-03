// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
  pthread_mutex_init(&clients_mutex, NULL);
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&clients_mutex);
  server_lock_p lock = lock_manager[lid];
  if (lock == NULL) {
    lock = (server_lock_p) new server_lock;
    lock->server_state = none;
    lock_manager[lid] = lock;
  }
  switch (lock->server_state) {
    case none: {
      lock->client_holding = id;
      lock->server_state = locked;
      pthread_mutex_unlock(&clients_mutex);
      return ret;
    }
    case locked: {
      lock->server_state = revoking;
      lock->clients_queue.push_back(id);
      pthread_mutex_unlock(&clients_mutex);
      handle(lock->client_holding).safebind()->call(rlock_protocol::revoke, lid, r);
      return lock_protocol::RETRY;
    }
    case revoking: {
      lock->clients_queue.push_back(id);
      pthread_mutex_unlock(&clients_mutex);
      return lock_protocol::RETRY;
    }
    case retrying: {
      // the retrying client retried
      if(id == lock->client_retrying) {
        lock->client_holding = id;
        lock->client_retrying.clear();
        lock->server_state = locked;
        // revoke right away when someone else is waiting
        if(!lock->clients_queue.empty()) {
          lock->server_state = revoking;
          pthread_mutex_unlock(&clients_mutex);
          handle(id).safebind()->call(rlock_protocol::revoke, lid, r);
          return lock_protocol::OK;
        } else {
          pthread_mutex_unlock(&clients_mutex);
          return lock_protocol::OK;
        }
      }
      // other clients acquire
      else {
        lock->clients_queue.push_back(id);
        pthread_mutex_unlock(&clients_mutex);
        return lock_protocol::RETRY;
      }
    }
    default:{
      return lock_protocol::NOENT;
    }
  }
  
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&clients_mutex);
  server_lock_p lock = lock_manager[lid];
  lock->client_holding.clear();
  if (!lock->clients_queue.empty()) {
    // std::set<std::string>::iterator next_client_p = lock->clients_queue.begin();
    // std::string next_client = *(next_client_p);
    // lock->clients_queue.erase(next_client_p);
    std::string next_client = lock->clients_queue.front();
    lock->clients_queue.pop_front();
    lock->client_retrying = next_client;
    lock->server_state = retrying;
    pthread_mutex_unlock(&clients_mutex);
    handle(next_client).safebind()->call(rlock_protocol::retry, lid, r);
  } else {
    lock->server_state = none;
    pthread_mutex_unlock(&clients_mutex);
  }
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

