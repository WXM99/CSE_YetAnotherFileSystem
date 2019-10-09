// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client(lock_dst);
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
}


yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
yfs_client::issymlink(inum inum) {
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }
    if (a.type == extent_protocol::T_SLINK) {
        printf("isfile: %lld is a symlink\n", inum);
        return true;
    }
    printf("isfile: %lld is not a symlink\n", inum);
    return false;
}

bool
yfs_client::isdir(inum inum)
{
    // Oops! is this still correct when you implement symlink?
    // return ! isfile(inum);
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_DIR) {
        printf("isfile: %lld is a dir\n", inum);
        return true;
    } 
    printf("isfile: %lld is not a dir\n", inum);
    return false;
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */

    std::string buf;
    r = ec->get(ino, buf);
    if (r != OK)
        return r;
    buf.resize(size);
    r = ec->put(ino, buf);
    if (r != OK)
        return r;
    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */

    bool found;
    lookup(parent, name, found, ino_out);
    if (found) {
        return EXIST;
    }

    ec->create(extent_protocol::T_FILE, ino_out);
    std::string buf, new_entry_str;
    if (ec->get(parent, buf) != extent_protocol::OK) {
        exit(0);
    }
    struct diy_dirent new_entry;
    new_entry.inum = ino_out;
    new_entry.name_length = (unsigned short) strlen(name);
    memcpy(new_entry.name, name, new_entry.name_length);
    new_entry_str.assign((char *) (&new_entry), sizeof(diy_dirent));
    buf.append(new_entry_str);
    ec->put(parent, buf);
    return r;
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */

    bool found;
    lookup(parent, name, found, ino_out);
    if (found)
        return EXIST;
    std::string buf, new_entry_str;
    if (ec->get(parent, buf) != extent_protocol::OK) {
        exit(0);
    }

    ec->create(extent_protocol::T_DIR, ino_out);
    struct diy_dirent new_entry;
    new_entry.inum = ino_out;
    new_entry.name_length = (unsigned short) strlen(name);
    memcpy(new_entry.name, name, new_entry.name_length);
    new_entry_str.assign((char *) (&new_entry), sizeof(diy_dirent));
    buf.append(new_entry_str);
    ec->put(parent, buf);
    return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */

    std::list<dirent> entries;
    readdir(parent, entries);
    std::string name_str;
    while (entries.size() != 0) {
        dirent dir_ent = entries.front();
        entries.pop_front();
        if (dir_ent.name == name_str.assign(name, strlen(name))) {
            found = true;
            ino_out = dir_ent.inum;
            return r;
        }
    }
    found = false;
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */

    std::string buf;
    if (ec->get(dir, buf) != extent_protocol::OK) {
        exit(0);
    }
    extent_protocol::attr attr;
    ec->getattr(dir, attr);
    if (attr.type != extent_protocol::T_DIR) {
        exit(0);
    }
    const char *cbuf = buf.c_str();
    unsigned int size = (unsigned int) buf.size();
    unsigned int entry_num = size / (sizeof(diy_dirent));
    for (uint32_t i = 0; i < entry_num; i++) {
        struct diy_dirent tmp_entry;
        memcpy(&tmp_entry, cbuf + i * sizeof(diy_dirent), sizeof(diy_dirent));
        struct dirent dirent;
        dirent.inum = tmp_entry.inum;
        dirent.name.assign(tmp_entry.name, tmp_entry.name_length);
        list.push_back(dirent);
    }
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your code goes here.
     * note: read using ec->get().
     */
    std::string content;
    ec->get(ino, content);
    if ((unsigned int) off >= content.size()) {
        data.erase();
        return r;
    }
    data = content.substr(off, size);
    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */

    std::string content;
    ec->get(ino, content);
    std::string buf;
    buf.assign(data, size);

    if ((unsigned int) off <= content.size()) {
        content.replace(off, size, buf);
        bytes_written = size;
    } else {
        size_t old_size = content.size();
        content.resize(size + off, '\0');
        content.replace(off, size, buf);
        bytes_written = size + off - old_size;
    }
    ec->put(ino, content);
    return r;
}

int yfs_client::unlink(inum parent, const char *name)
{
    /*
     * your code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
    return rmdir(parent, name);
}

int yfs_client::rmdir(inum parent, const char *name)
{
    int r = OK;
    std::list<dirent> dir_entries;
    std::string name_str;
    name_str.assign(name, strlen(name));
    bool found = false;
    readdir(parent, dir_entries);
    std::list<dirent>::iterator it = dir_entries.begin();
    for (; it != dir_entries.end(); ++it) {
        if (it->name == name_str) {
            found = true;
            break;
        }
    }
    if (!found)
        return NOENT;
    dir_entries.erase(it);
    std::string buf;
    for (it = dir_entries.begin(); it != dir_entries.end(); ++it) {
        std::string dir_entry_left_str;
        diy_dirent dir_entry_left;
        dir_entry_left.inum = it->inum;
        dir_entry_left.name_length = (unsigned short) it->name.size();
        memcpy(dir_entry_left.name, it->name.data(), dir_entry_left.name_length);
        dir_entry_left_str.assign((char *) (&dir_entry_left), sizeof(diy_dirent));
        buf.append(dir_entry_left_str);
    }

    ec->put(parent, buf);
    return r;
}

int
yfs_client::readlink(inum ino, std::string &data)
{
    int r = OK;
    std::string buf;
    r = ec->get(ino, buf);
    data = buf;
    return r;
}

int
yfs_client::symlink(inum parent, const char *name, const char *link, inum &ino_out) {
    int r = OK;
    std::string parent_content, parent_add;
    r = ec->get(parent, parent_content);
    bool found;
    inum id;
    lookup(parent, name, found, id);
    if (found)
        return EXIST;
    r = ec->create(extent_protocol::T_SLINK, ino_out);
    r = ec->put(ino_out, std::string(link));
    struct diy_dirent sym_entry;
    sym_entry.inum = ino_out;
    sym_entry.name_length = (unsigned short) strlen(name);
    memcpy(sym_entry.name, name, sym_entry.name_length);
    parent_add.assign((char *) (&sym_entry), sizeof(diy_dirent));
    parent_content += parent_add;
    ec->put(parent, parent_content);
    return r;
}

