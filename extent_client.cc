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
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p new_file = (cached_file_p) new cached_file();
  new_file->created = true;
  new_file->buf_valid = true;
  new_file->attr_valid = true;
  new_file->type = type;
  new_file->attr.atime = time(NULL);
  new_file->attr.ctime = time(NULL);
  new_file->attr.mtime = time(NULL);
  new_file->attr.type = type;
  if (cache[id] != NULL)
    delete cache[id];
  cache[id] = new_file;
  // Your lab2 part1 code goes here
  ret = cl->call(extent_protocol::create, type, id);
  return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  // Your lab2 part1 code goes here
  cached_file_p file = cache[eid];
  // cache hit
  if (file != NULL && file->buf_valid) {
    buf = file->buf;
    return ret;
  }
  // cache miss
  if (file == NULL)
    file = (cached_file_p) new cached_file();
  
  std::string cache_buf;
  ret = cl->call(extent_protocol::get, eid, cache_buf);
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
    attr = file->attr;
    return ret;
  }
  // cache miss
  if (file == NULL)
    file = (cached_file_p) new cached_file();

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
  
  file->dirty = true;
  file->buf_valid = true;
  file->buf = buf;
  file->attr.atime = time(NULL);
  file->attr.ctime = time(NULL);
  file->attr.mtime = time(NULL);
  file->attr.size = buf.size();
  
  cache[eid] = file;
  // Your lab2 part1 code goes here
  int r;
  ret = cl->call(extent_protocol::put, eid, buf,r);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  // Your lab2 part1 code goes here
  std::map<extent_protocol::extentid_t, cached_file_p>::iterator iter;
  for(iter = cache.begin(); iter != cache.end(); iter++) {
    if (iter->first == eid) {
      cache.erase(iter);
      break;
    }
  }
  int r;
  ret = cl->call(extent_protocol::remove, eid, r);
  return ret;
}



extent_protocol::status 
extent_client::sync(extent_protocol::extentid_t eid) {
  extent_protocol::status ret = extent_protocol::OK;
  cached_file_p file = cache[eid];
  if (file == NULL) {
    return ret;
  }
  file->buf_valid = false;
  file->attr_valid = false;
  
  return ret;
}