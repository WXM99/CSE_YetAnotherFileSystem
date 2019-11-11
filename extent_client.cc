// Lab4 cache protocol envolved
// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

// a demo to show how to use RPC
extent_protocol::status
extent_client::create(uint32_t type, extent_protocol::extentid_t &id)
{
  // cached locally
  printf("==================created\n");
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p new_file = (cached_file_p) new cached_file();
  new_file->created = true;
  new_file->buf_valid = true;
  new_file->attr_valid = true;
  new_file->type = type;
  new_file->attr.atime = time(NULL);
  new_file->attr.ctime = time(NULL);
  new_file->attr.mtime = time(NULL);
  if (cache[id] != NULL)
    delete cache[id];
  cache[id] = new_file;
  ret = cl->call(extent_protocol::create, type, id);
  printf("=================cached\n");
  return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p file = cache[eid];
  // cache hit
  if (file != NULL && file->buf_valid) {
    if (file->removed)
      return ret;
    buf = file->buf;
    return ret;
  }
  // cache miss
  if (file == NULL)
    file = (cached_file_p) new cached_file();
  if (file->removed)
    return ret; 
  
  std::string cache_buf;
  cl->call(extent_protocol::get, eid, cache_buf);
  file->buf=cache_buf;
  file->buf_valid = true;
  file->attr.atime = time(NULL);

  cache[eid] = file;
  buf = cache[eid]->buf;
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p file = cache[eid];
  // cache hit
  if (file != NULL && file->attr_valid) {
    if (file->removed)
      return ret; 
    attr = file->attr;
    return ret;
  }
  // cache miss
  if (file == NULL)
    file = (cached_file_p) new cached_file();

  if (file->removed)
    return ret;

  extent_protocol::attr new_attr;
  ret = cl->call(extent_protocol::getattr, eid, new_attr);
  file->attr = new_attr;
  file->attr_valid = true;

  cache[eid] = file;
  attr = cache[eid]->attr;
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p file = cache[eid];
  if (file == NULL)
    file = (cached_file_p) new cached_file();
  if (file->removed)
    return ret;  
  
  file->dirty = true;
  file->buf_valid = true;
  file->buf = buf;
  file->attr.atime = time(NULL);
  file->attr.ctime = time(NULL);
  file->attr.mtime = time(NULL);
  file->attr.size = buf.size();
  
  cache[eid] = file;

  // int r;
  // ret = cl->call(extent_protocol::put, eid, buf,r);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p file = cache[eid];
  if (file == NULL)
    file = (cached_file_p) new cached_file();

  file->removed = true;
  file->created = false;
  file->dirty = false;
  file->attr_valid = false;
  file->buf_valid = false;
  cache[eid] = file;
  // int r;
  // ret = cl->call(extent_protocol::remove, eid, r);
  return ret;
}


extent_protocol::status 
extent_client::sync2server(extent_protocol::extentid_t eid) {
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p file = cache[eid];
  if (file == NULL) {
    return ret;
  }
  file->buf_valid = false;
  file->attr_valid = false;

  bool created = file->created;
  bool removed = file->removed;
  bool dirty = file->dirty;

  uint16_t operation = created | (removed << 1) | (dirty << 2);
  // created: 001
  // removed: 010
  // dirty  : 100
  int r;
  switch (operation)
  {
    case 0x01: // 001
      ret = cl->call(extent_protocol::create, file->type, eid);
      return ret;
    case 0x02: // 010
      ret = cl->call(extent_protocol::remove, eid, r);
      return ret;
    case 0x04: // 100 
      ret = cl->call(extent_protocol::put, eid, file->buf,r);
      return ret;
    case 0x05: // 101 create then dirty 
      ret = cl->call(extent_protocol::create, file->type, eid);
      ret = cl->call(extent_protocol::put, eid, file->buf,r);
      return ret;
    case 0x06: // 110 dirty and removed impossible
    case 0x03: // 011 create then remove impossible
    case 0x07: // 111 impossible
    case 0x00: // 000 read only
    default:
      break;
  }
  return ret;
}