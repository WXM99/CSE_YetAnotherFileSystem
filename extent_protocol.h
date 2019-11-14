// extent wire protocol

#ifndef extent_protocol_h
#define extent_protocol_h

#include "rpc.h"

class extent_protocol {
 public:
  typedef int status;
  typedef unsigned long long extentid_t;
  enum xxstatus { OK, RPCERR, NOENT, IOERR };
  enum rpc_numbers {
    put = 0x6001,
    get,
    getattr,
    remove,
    create
  };

  enum types {
    T_DIR = 1,
    T_FILE,
    T_SLINK
  };

  struct attr {
    uint32_t type;
    unsigned int atime;
    unsigned int mtime;
    unsigned int ctime;
    unsigned int size;
  };
  struct full_file {
    extent_protocol::attr attr;
    std::string buf;
  };
};

inline unmarshall &
operator>>(unmarshall &u, extent_protocol::attr &a)
{
  u >> a.type;
  u >> a.atime;
  u >> a.mtime;
  u >> a.ctime;
  u >> a.size;
  return u;
}

inline marshall &
operator<<(marshall &m, extent_protocol::attr a)
{
  m << a.type;
  m << a.atime;
  m << a.mtime;
  m << a.ctime;
  m << a.size;
  return m;
}

inline unmarshall &
operator>>(unmarshall &u, extent_protocol::full_file &f)
{
  u >> f.attr.type;
  u >> f.attr.atime;
  u >> f.attr.mtime;
  u >> f.attr.ctime;
  u >> f.attr.size;
  u >> f.buf;
  return u;
}

inline marshall &
operator<<(marshall &m, extent_protocol::full_file f)
{
  m << f.attr.type;
  m << f.attr.atime;
  m << f.attr.mtime;
  m << f.attr.ctime;
  m << f.attr.size;
  m << f.buf;
  return m;
}

#endif 
