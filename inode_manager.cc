#include "inode_manager.h"

// helpers

/*
 * constraint the block id
 */
void check_block_id(const blockid_t id, const char* where) {
  if (id >= BLOCK_NUM){
    printf("\t%s: error! block id-%d is out of disk\n",where, id);
    exit(0);
  }
}

/*
 * make buffer pointer not null
 */
void check_buf_pointer(const char* buf, const char* where) {
  if(buf == NULL) {
    printf("\t%s: error! dest buf is NULL\n", where);
  }
}

/*
 * op == c: check a block bit if is free or not in its bitmap block
 * op == a: alloc a block bit in its bitmap block
 * op == f: free a block bit in its bitmap block
 * id: the block id
 * block_in_bitmap: pointing to the block in bitmap where holds the bit for block
 * op: check, alloc or free
 */
bool bitmap_block_manager(const blockid_t id, char* block_in_bitmap, const char op) {
  // the bit in the bitmap block
  unsigned int bits_count = id % BPB;
  // the byte index in the bitmap block
  unsigned int byte_index = bits_count / 8;
  // the bit index in the byte
  unsigned int bit_index = bits_count % 8;
  // the the byte in the bitmap block
  char bitmap_byte = block_in_bitmap[byte_index];
  // the mask to get the bit out
  const char mask = 0x01 << bit_index;
  if (op == 'c') // for check
  {
    return !(bitmap_byte & mask);
  }
  else if (op == 'a') // for alloc
  {
    bitmap_byte = bitmap_byte | mask;
    void* cp_status = memcpy(block_in_bitmap+byte_index, &bitmap_byte, 1);
    return cp_status == NULL;
  }
  else if (op == 'f') // for free
  {
    bitmap_byte = bitmap_byte & (~mask);
    void* cp_status = memcpy(block_in_bitmap+byte_index, &bitmap_byte, 1); 
    return cp_status == NULL;
  }
  else {
    printf("\tbpbm: error! unknown operation charactor: %c\n", op);
    exit(0);
  }
}

bool check_free_in_bitmap(const blockid_t id, disk* d) {
  check_block_id(id, "BM");
  blockid_t bitmap_blockid = BBLOCK(id);
  char bitmap_block[BLOCK_SIZE];
  d->read_block(bitmap_blockid, bitmap_block);
  return bitmap_block_manager(id, bitmap_block, 'c');
}

void alloc_bitmap(blockid_t id, disk* d) {
  check_block_id(id, "BM");
  blockid_t bitmap_block_id = BBLOCK(id);
  char bitmap_block[BLOCK_SIZE];
  d->read_block(bitmap_block_id, bitmap_block);
  bitmap_block_manager(id, bitmap_block, 'a');
  d->write_block(id, bitmap_block);
}

void free_bitmap(blockid_t id, disk* d) {
  check_block_id(id, "BM");
  blockid_t bitmap_block_id = BBLOCK(id);

  char bitmap_block[BLOCK_SIZE];
  d->read_block(bitmap_block_id, bitmap_block);
  bitmap_block_manager(id, bitmap_block, 'f');
  d->write_block(id, bitmap_block);
}

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  check_block_id(id, "disk");
  check_buf_pointer(buf, "disk");
  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  check_block_id(id, "disk");
  check_buf_pointer(buf, "disk");
  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  blockid_t start_at_data = IBLOCK(INODE_NUM, BLOCK_NUM);
  for (blockid_t i = start_at_data; i < BLOCK_NUM; i++) {
    int allocated = using_blocks[i];
    if (!allocated) {
      using_blocks[i] = 1;
      alloc_bitmap(i, d);
      return i;
    }
  }
  printf("\t bm:error! no block is free\n");
  exit(0);
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  check_block_id(id, "bm");
  free_bitmap(id, d);
  using_blocks[id] = 0;
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  uint32_t inode_id_start = 1;
  for (uint32_t i = inode_id_start; i < INODE_NUM+1; i++) {
    blockid_t inode_blockid = IBLOCK(i, BLOCK_NUM);
    blockid_t inode_bitmap_blockid = BBLOCK(inode_blockid);
    char bitmap_block[BLOCK_SIZE];
    bm->read_block(inode_bitmap_blockid, bitmap_block);
    bool is_free_block = bitmap_block_manager(inode_blockid, bitmap_block, 'c');
    if (is_free_block) {
      bitmap_block_manager(inode_blockid, bitmap_block, 'a');
      inode tmp_inodep[1];
      tmp_inodep->type = type;
      tmp_inodep->size = 0;
      tmp_inodep->atime = time(NULL);
      char inode_block[BLOCK_SIZE];
      memcpy(inode_block, tmp_inodep, sizeof(inode));
      bm->write_block(inode_blockid, inode_block);
      return i;
    }
  }
  printf("\tim:error! no inode is free\n");
  exit(0);
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */

  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  blockid_t inode_blockid = IBLOCK(inum, BLOCK_NUM);
  char inode_block[BLOCK_SIZE];
  bm->read_block(inode_blockid, inode_block);
  inode* tar_inode = (inode*) inode_block;
  a.atime = tar_inode->atime;
  a.ctime = tar_inode->ctime;
  a.mtime = tar_inode->ctime;
  a.size = tar_inode->size;
  a.type = tar_inode->type;
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  
  return;
}
