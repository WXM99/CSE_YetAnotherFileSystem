// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "extent_server.h"

class extent_client {
 private:
  rpcc *cl;

  struct cached_file {
    uint32_t type;
    std::string buf;
    extent_protocol::attr attr;
    bool buf_valid;
    bool attr_valid;
    bool dirty;
    cached_file() {
      buf_valid = false;
      attr_valid = false;
      dirty = false;
    }
  };
  typedef cached_file* cached_file_p; 
  std::map<extent_protocol::extentid_t, cached_file_p> cache;
 public:
  extent_client(std::string dst);

  extent_protocol::status create(uint32_t type, extent_protocol::extentid_t &eid);
  extent_protocol::status get(extent_protocol::extentid_t eid, 
			                        std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				                          extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status sync(extent_protocol::extentid_t eid);
};

#endif 

