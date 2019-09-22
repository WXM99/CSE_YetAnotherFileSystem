#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
    memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
    memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
    blockid_t start_at = IBLOCK(INODE_NUM, BLOCK_NUM) + 1;
    for (blockid_t i = start_at; i < BLOCK_NUM; i++) {
        int is_used = using_blocks[i];
        if (is_used == 0) {
            using_blocks[i] = 1;
            return i;
        }
    }
    printf("\tbm:error! no more free block to alloc.\n");
    exit(0);
}

void
block_manager::free_block(uint32_t id)
{
    using_blocks[id] = 0;
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

// private helpers
#define ROUND_UP_DEVISION(a, b) { (a/b) + (a%b==0 ? 0:1) }

blockid_t
inode_manager::get_blockid_in_inode(struct inode *ino, uint32_t index) {
    if (index < NDIRECT) {
        return ino->blocks[index];
    }
    blockid_t inblock_id = ino->blocks[NDIRECT];
    char inblock[BLOCK_SIZE];
    bm->read_block(inblock_id, inblock);
    return (((blockid_t *) inblock)[index - NDIRECT]);
}

void
inode_manager::read_block_in_inode(struct inode *ino, uint32_t index, std::string &buf) {
    blockid_t bl_id = get_blockid_in_inode(ino, index);
    char content[BLOCK_SIZE];
    bm->read_block(bl_id, content);
    buf.assign(content, BLOCK_SIZE);
}

void
inode_manager::write_block_in_inode(struct inode *ino, uint32_t index, std::string &buf) {
    blockid_t bl_id = get_blockid_in_inode(ino, index);
    bm->write_block(bl_id, buf.data());
}


void
inode_manager::alloc_block_in_inode(struct inode *ino, uint32_t index, std::string &buf, bool write_through) {
    blockid_t blk_id = bm->alloc_block();
    if (write_through) {
        bm->write_block(blk_id, buf.data());
    }
    if (index < NDIRECT) {
        ino->blocks[index] = blk_id;
    }
    else {
        blockid_t inblock_id = ino->blocks[NDIRECT];
        char inblock[BLOCK_SIZE];
        bm->read_block(inblock_id, inblock);
        (((blockid_t *) inblock)[index - NDIRECT]) = blk_id;
        bm->write_block(inblock_id, inblock);
    }
}

void
inode_manager::free_block_in_inode(struct inode *ino, uint32_t index) {
    blockid_t id = get_blockid_in_inode(ino, index);
    bm->free_block(id);
}

// public methods
inode_manager::inode_manager()
{
  bm = new block_manager();
  struct inode root;
  root.type = extent_protocol::T_DIR;
  root.size = 0;
  root.atime = time(NULL);
  put_inode(1, &root);
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
    uint32_t inum = 0;
    inode *ino;
    for (uint32_t i = 2; i < INODE_NUM+1; i++) {
        ino = get_inode(i);
        if (ino->type == 0) {
            inum = i;
            break;
        } else {
            free(ino);
        }
    }
    if (!inum) {
        exit(0);
    }
    ino->type = (short) type;
    ino->size = 0;
    ino->atime = time(NULL);
    put_inode(inum, ino);
    free(ino);
    return inum;
}

void
inode_manager::free_inode(uint32_t inum)
{
    struct inode *ino = get_inode(inum);
    if (!ino) {
        exit(0);
    }
    ino->type = 0;
    put_inode(inum, ino);
    free(ino);
    return;
}

/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
    struct inode *ino, *ino_disk;
    char buf[BLOCK_SIZE];
    if (inum <= 0 || inum > INODE_NUM) {
        exit(0);
    }
    bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
    ino_disk = (struct inode *) buf + inum % IPB;
    ino = (struct inode *) malloc(sizeof(struct inode));
    *ino = *ino_disk;
    return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
    char buf[BLOCK_SIZE];
    struct inode *ino_disk;
    assert(ino);
    bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
    ino_disk = (struct inode *) buf + inum % IPB;
    *ino_disk = *ino;
    bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
    struct inode *ino = get_inode(inum);
    *size = ino->size;
    if (ino->size == 0){
        return;
    }
    if (ino->size > MAXFILE * BLOCK_SIZE) {
        exit(0);
    }
    uint32_t block_num = ROUND_UP_DEVISION(ino->size, BLOCK_SIZE);
    char *buf_content = (char *) malloc(BLOCK_NUM * BLOCK_SIZE);
    std::string content;
    for (uint32_t i = 0; i < block_num; i++) {
        read_block_in_inode(ino, i, content);
        memcpy(buf_content + i * BLOCK_SIZE, content.data(), BLOCK_SIZE);
    }
    *buf_out = buf_content;
    ino->atime = time(NULL);
    put_inode(inum, ino);
    free(ino);
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
    inode *ino = get_inode(inum);
    uint32_t blk_num_ori = ROUND_UP_DEVISION(ino->size, BLOCK_SIZE);
    uint32_t blk_num_new = ROUND_UP_DEVISION((unsigned int)size, BLOCK_SIZE);
    
    std::string content;
    if (blk_num_new < blk_num_ori) {
        for (uint32_t start = blk_num_new; start < blk_num_ori; start++) {
            free_block_in_inode(ino, start);
        }
    }
    else if (blk_num_new > blk_num_ori) {
        for (uint32_t start = blk_num_ori; start < blk_num_new; start++) {
            alloc_block_in_inode(ino, start, content, false);
        }
    }
    ino->size = (unsigned int) size;
    if (blk_num_new != 0) {
        uint32_t start = 0;
        for (; start + 1 < blk_num_new; start++) {
            content.assign(buf + BLOCK_SIZE * start, BLOCK_SIZE);
            write_block_in_inode(ino, start, content);
        }
        uint32_t padding_bytes = blk_num_new * BLOCK_SIZE - size;
        uint32_t tail_bytes = BLOCK_SIZE - padding_bytes;
        content.assign(buf + BLOCK_SIZE * start, tail_bytes);
        content.resize(BLOCK_SIZE);
        write_block_in_inode(ino, blk_num_new - 1, content);
    }
    ino->atime = time(NULL);
    ino->mtime = time(NULL);
    ino->ctime = time(NULL);
    put_inode(inum, ino);
    free(ino);
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
    inode *ino = get_inode(inum);
    a.type = (uint32_t) ino->type;
    a.atime = ino->atime;
    a.ctime = ino->ctime;
    a.mtime = ino->mtime;
    a.size = ino->size;
    free(ino);
}

void
inode_manager::remove_file(uint32_t inum)
{
    inode *ino = get_inode(inum);
    uint32_t size = ino->size;
    uint32_t block_num = size == 0 ? 0 : ((size - 1) / BLOCK_SIZE + 1);
    for (uint32_t start = 0; start < block_num; start++) {
        free_block_in_inode(ino, start);
    }
    free_inode(inum);
    free(ino);
}
