// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKETSZ 13
#define BCACHE_HASH(dev, blk) (((dev << 27) | blk) % BUCKETSZ)
#define NULL 0

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf bucket_head[BUCKETSZ];
  struct spinlock bucket_lock[BUCKETSZ];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  for (int i = 0; i < BUCKETSZ; ++i) {
    initlock(&bcache.bucket_lock[i], "bcache bucket");
    bcache.bucket_head[i].next = NULL;
  }
  // 使用单向链表
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->lastuse = 0;
    b->refcnt = 0;
    b->next = bcache.bucket_head[0].next;
    initsleeplock(&b->lock, "buffer");
    bcache.bucket_head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint idx = BCACHE_HASH(dev, blockno);

  acquire(&bcache.bucket_lock[idx]);

  // Is the block already cached?
  for(b = bcache.bucket_head[idx].next; b != NULL; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lock[idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket_lock[idx]);
  // may be interrupt
  acquire(&bcache.lock);

  // check again

  acquire(&bcache.bucket_lock[idx]);

  // Is the block already cached?
  for(b = bcache.bucket_head[idx].next; b != NULL; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lock[idx]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket_lock[idx]);


  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  struct buf *pre_lru = 0;
  int lru_idx = -1;

  for (int i = 0; i < BUCKETSZ; i++) {
    acquire(&bcache.bucket_lock[i]);
    int found_new = 0;
    for(b = &bcache.bucket_head[i]; b->next != NULL; b = b->next) {
      if (b->next->refcnt == 0 && (!pre_lru || pre_lru->next->lastuse > b->next->lastuse)) {
        pre_lru = b;
        found_new = 1;
      }
    }
    if(!found_new) {
      release(&bcache.bucket_lock[i]);
    } else {
      if (lru_idx != -1)
        release(&bcache.bucket_lock[lru_idx]);
      lru_idx = i;
    }
  }

  if (!pre_lru)
    panic("bget: no buffers");

  struct buf *lru = pre_lru->next;


  if (lru_idx != idx) {
    pre_lru->next = lru->next;
    release(&bcache.bucket_lock[lru_idx]);
    acquire(&bcache.bucket_lock[idx]);
    lru->next = bcache.bucket_head[idx].next;
    bcache.bucket_head[idx].next = lru;
  } 
  
  lru->dev = dev;
  lru->blockno = blockno;
  lru->valid = 0;
  lru->refcnt = 1;
  release(&bcache.bucket_lock[idx]);
  release(&bcache.lock);
  acquiresleep(&lru->lock);
  return lru;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  uint idx = BCACHE_HASH(b->dev, b->blockno);

  acquire(&bcache.bucket_lock[idx]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->lastuse = ticks;
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.bucket_head[idx].next;
    // b->prev = &bcache.bucket_head[idx];
    // bcache.bucket_head[idx].next->prev = b;
    // bcache.bucket_head[idx].next = b;
  }
  
  release(&bcache.bucket_lock[idx]);
}

void
bpin(struct buf *b) {
  uint idx = BCACHE_HASH(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[idx]);
  b->refcnt++;
  release(&bcache.bucket_lock[idx]);
}

void
bunpin(struct buf *b) {
  uint idx = BCACHE_HASH(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[idx]);
  b->refcnt--;
  release(&bcache.bucket_lock[idx]);
}
