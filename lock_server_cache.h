#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>


#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"
#include <pthread.h>
#include <deque>


class lock_server_cache {
 private:
  int nacquire;
  pthread_mutex_t clients_mutex;
  enum server_states_t {
    none = 0,
    locked,
    revoking,
    retrying
  };
  struct server_lock {
    server_states_t server_state;
    std::string client_holding;
    std::string client_retrying;
    std::deque<std::string> clients_queue;
  };
  typedef server_lock* server_lock_p;
  std::map<lock_protocol::lockid_t, server_lock_p> lock_manager;
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
