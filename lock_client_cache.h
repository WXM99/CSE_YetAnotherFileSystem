// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"
#include <pthread.h>
#include <map>
#include <deque>


// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 6.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
  pthread_mutex_t threads_mutex;
  enum client_states_t {
    none = 0,
    free,
    locked,
    acquiring,
    releasing
  };
  enum server_response_t {
    empty = 0,
    revoke,
    retry,
    RETRY
  };
  struct client_cached_lock {
    std::deque<pthread_cond_t*> threads_queue;
    client_states_t client_state;
    server_response_t server_response;
  };
  typedef client_cached_lock* cached_lock_p;
  std::map<lock_protocol::lockid_t, cached_lock_p> lock_cache;
  lock_protocol::status rpc_acquire(lock_protocol::lockid_t, cached_lock_p, pthread_cond_t*);
  void xlock(lock_protocol::lockid_t, const char*);
 public:
  static int last_port;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);
};


#endif
