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
      bm->write_block(inode_blockid, (char*)tmp_inodep);
      bm->write_block(inode_bitmap_blockid, bitmap_block);
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
  blockid_t inode_blockid = IBLOCK(inum, BLOCK_NUM);
  blockid_t inode_bitmap_blockid = BBLOCK(inode_blockid);
  char bitmap_block[BLOCK_SIZE];
  bm->read_block(inode_bitmap_blockid, bitmap_block);
  bool is_free_block = bitmap_block_manager(inode_blockid, bitmap_block, 'c');
  if(is_free_block) {
    return;
  }
  // free a bit in bit map
  bitmap_block_manager(inode_blockid, bitmap_block, 'f');
  bm->write_block(inode_bitmap_blockid, bitmap_block);
  // free inode in inode table
  inode free_inode[1];
  free_inode -> type = 0;
  bm->write_block(inode_blockid, (char*)free_inode);
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
typedef struct block_entity {
  char content[BLOCK_SIZE];
} block_entity_t;


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
  struct inode *tar_inode = get_inode(inum);
  unsigned int file_size = tar_inode->size;
  unsigned int read_size = MIN(file_size, MAXFILE*BLOCK_SIZE);
  *size = read_size;
  unsigned int read_block_num = read_size / BLOCK_SIZE + ((read_size % BLOCK_SIZE) > 0 ? 1:0);
  block_entity_t *buf_in_block = (block_entity_t*) malloc(read_block_num * BLOCK_SIZE);

  if (read_size > MAXFILE*BLOCK_SIZE) {
    printf("\tim: error! read file size(%d) is too large\n", read_size);
    free(tar_inode);
    exit(0);
  }
  char tmp_block[BLOCK_SIZE];
  // cpy the whole direct block
  unsigned int direct_block_num = MIN(read_block_num, NDIRECT);
  for (unsigned int i = 0; i < direct_block_num; i++) {
    blockid_t tmp_direct_blockid = tar_inode->blocks[i];
    bm->read_block(tmp_direct_blockid, tmp_block);
    memcpy( buf_in_block + i, tmp_block, BLOCK_SIZE); 
  }
  // cpy rest bytes in direct block
  unsigned int rest_byte_num = read_size % BLOCK_SIZE;
  if (direct_block_num < NDIRECT && rest_byte_num > 0) {
    bm->read_block(direct_block_num, tmp_block);
    memcpy( buf_in_block + direct_block_num, tmp_block, rest_byte_num);
  }
  // cpy in indirect block
  if (read_size > NDIRECT * BLOCK_SIZE) {
    blockid_t indirect_blockid = tar_inode->blocks[NDIRECT];
    char indirect_block[BLOCK_SIZE];
    bm->read_block(indirect_blockid, indirect_block);
    unsigned int over_bytes = read_size - (NDIRECT * BLOCK_SIZE);
    unsigned int over_block_num = over_bytes / BLOCK_SIZE;
    unsigned int rest_byte_num = over_bytes % BLOCK_SIZE;
    blockid_t *data_blockid = (blockid_t *) indirect_block;
    for (unsigned int i = 0; i < over_block_num; i++) {
      bm->read_block(data_blockid[i], tmp_block);
      memcpy( buf_in_block + NDIRECT + i, tmp_block, BLOCK_SIZE);
    }
    if (rest_byte_num > 0) {
      bm->read_block(data_blockid[over_block_num], tmp_block);
      memcpy( buf_in_block + NDIRECT + over_block_num, tmp_block, rest_byte_num);
    }
  }
  free(tar_inode);
  *buf_out = (char*) buf_in_block;
  return;
}

/* alloc/free blocks if needed */
#define USED_INDIR(block_nums) (block_nums > NDIRECT)
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  if (size < 0 || size > (int)(MAXFILE*BLOCK_SIZE)) {
    printf("\tim: error! invalid size: %d\n", size);
    exit(0);
  }

  unsigned int whole_block_num = size / BLOCK_SIZE;
  unsigned int rest_byte_num = size % BLOCK_SIZE;
  unsigned int real_block_num = whole_block_num + (rest_byte_num > 0 ? 1 : 0);

  struct inode *tar_inode = get_inode(inum);
  unsigned int ori_size = tar_inode->size;
  unsigned int ori_block_num = (ori_size / BLOCK_SIZE) + ((ori_size % BLOCK_SIZE) > 0 ? 1 : 0);

  unsigned int overwrite_block_num = MIN(ori_block_num , real_block_num);
  const block_entity_t* buf_in_block = (block_entity_t*) buf;
  
  // both old and new files' sizes are within 100 blk;
  if (!USED_INDIR(real_block_num) && !USED_INDIR(ori_block_num)) {
    // overwrite the overlapped blks;
    for (unsigned int i = 0; i < overwrite_block_num; i++) {
      blockid_t tmp_blockid = tar_inode->blocks[i];
      bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
    }
    // new file is shorter, free the redundant blks.
    if (real_block_num <= ori_block_num) {
      for (unsigned int i = real_block_num; i < ori_block_num; i++) {
        blockid_t tmp_blockid = tar_inode->blocks[i];
        bm->free_block(tmp_blockid);
      }
    } 
    // old file is shorter, alloc more blks and fill them.
    else {
      for (unsigned int i = ori_block_num; i < real_block_num; i++) {
        blockid_t tmp_blockid = bm->alloc_block();
        bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
        tar_inode->blocks[i] = tmp_blockid;
        char test_read[BLOCK_SIZE];
        bm->read_block(tmp_blockid, test_read);
      }
    }
  }

  // new fill is fewer than 100 blk yet old one is larger than 100 blk
  else if (!USED_INDIR(real_block_num) && USED_INDIR(ori_block_num)) {
    // overwrite the overlapped blks;
    for (unsigned int i = 0; i < overwrite_block_num; i++) {
      blockid_t tmp_blockid = tar_inode->blocks[i];
      bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
    }
    // free the redundant blks.
      // free direct blocks
    for (unsigned int i = real_block_num; i < NDIRECT; i++) {
      blockid_t tmp_blockid = tar_inode->blocks[i];
      bm->free_block(tmp_blockid);
    }
      // free indirect blocks
    blockid_t indirect_blockid = tar_inode->blocks[NDIRECT];
    char indirect_block[BLOCK_SIZE];
    bm->read_block(indirect_blockid, indirect_block);
    unsigned int over_block_num = ori_block_num - real_block_num;
    blockid_t *data_blockid = (blockid_t*) indirect_block; 
    for (unsigned int i = 0; i <over_block_num; i++) {
      bm->free_block(data_blockid[i]);
    }
  }
  // new fill is larger than 100 blk yet old one is fewer 
  else if (USED_INDIR(real_block_num) && !USED_INDIR(ori_block_num)) {
    // overwrite the overlapped blks;
    for (unsigned int i = 0; i < overwrite_block_num; i++) {
      blockid_t tmp_blockid = tar_inode->blocks[i];
      bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
    }
    // alloc more blks
      // alloc all direct blks
      for (unsigned int i = overwrite_block_num; i < NDIRECT; i++) {
        blockid_t tmp_blockid = bm->alloc_block();
        bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
        tar_inode->blocks[i] = tmp_blockid;
      }
      // alloc indirect blks
      blockid_t indirect_blockid = bm->alloc_block();
      tar_inode->blocks[NDIRECT] = indirect_blockid;
      blockid_t data_blockids[NINDIRECT];
      for (unsigned int i = NDIRECT; i < real_block_num; i++)
      {
        blockid_t data_blockid = bm->alloc_block();
        data_blockids[i-NDIRECT] = data_blockid;
        bm->write_block(data_blockid, (char*) (buf_in_block + i));
      }
      bm->write_block(indirect_blockid, (char*) data_blockids);
  } 
  // both new fill and the old one are larger than 100 blk
  else {
    // overwrite the *100* overlapped blks;
    for (unsigned int i = 0; i < NDIRECT; i++) {
      blockid_t tmp_blockid = tar_inode->blocks[i];
      bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
    }
    // overwrite moreover indirect overlapped blks;
    blockid_t data_blockid[NINDIRECT];
    bm->read_block(tar_inode->blocks[NDIRECT], (char*) data_blockid);
    for (unsigned int i = NDIRECT; i < overwrite_block_num; i++) {
      blockid_t tmp_blockid = data_blockid[i-NDIRECT];
      bm->write_block(tmp_blockid, (char*) (buf_in_block + i));
    }
    // new file smaller, free indirect blks
    if (real_block_num <= ori_block_num) {
      for (unsigned int i = real_block_num; i < ori_block_num; i++) {
        blockid_t free_blockid = data_blockid[i-NDIRECT];
        bm->free_block(free_blockid);
      }
    }
    // old file smaller, alloc indirect blks
    else {
      for (unsigned int i = ori_block_num; i < real_block_num; i++) {
        blockid_t alloc_blockid = bm->alloc_block();
        bm->write_block(alloc_blockid, (char*) (buf_in_block+i));
        data_blockid[i-NDIRECT] = alloc_blockid;
      }
      bm->write_block(tar_inode->blocks[NDIRECT], (char*) data_blockid);
    }
  }
  tar_inode->size = size;
  put_inode(inum, tar_inode); 
  free(tar_inode);
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
  struct inode *tar_inode = get_inode(inum);
  unsigned int size = tar_inode->size;
  unsigned int block_num = (size / BLOCK_SIZE) + ((size % BLOCK_SIZE) > 0 ? 1 : 0);
  blockid_t* blocks = tar_inode->blocks;
  for (unsigned int  i = 0; i < block_num; i++) {
    bm->free_block(blocks[i]);
  }
  free_inode(inum);
  return;
}
