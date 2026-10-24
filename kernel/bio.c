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

#define NBUCKET 13

struct {
  struct spinlock lock[NBUCKET];
  struct spinlock evict;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;
  initlock(&bcache.evict, "bcache_evict");

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int h = (b - bcache.buf) % NBUCKET;
    b->next = bcache.head[h].next;
    b->prev = &bcache.head[h];
    initsleeplock(&b->lock, "buffer");
    bcache.head[h].next->prev = b;
    bcache.head[h].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  int h = blockno % NBUCKET;
  struct buf *b;

  acquire(&bcache.lock[h]);

  // Is the block already cached?
  for(b = bcache.head[h].next; b != &bcache.head[h]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[h]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.lock[h]);

  // Not cached.
  acquire(&bcache.evict);

  acquire(&bcache.lock[h]);

  for(b = bcache.head[h].next; b != &bcache.head[h]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[h]);
      release(&bcache.evict);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.lock[h]);

  for (int j = 0; j < NBUCKET; j++) {
    int k = (h + j) % NBUCKET;

    acquire(&bcache.lock[k]);

    for(b = bcache.head[k].prev; b != &bcache.head[k]; b = b->prev){
      if(b->refcnt == 0) {
        b->next->prev = b->prev;
        b->prev->next = b->next;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        release(&bcache.lock[k]);

        acquire(&bcache.lock[h]);

        b->next = bcache.head[h].next;
        b->prev = &bcache.head[h];
        bcache.head[h].next->prev = b;
        bcache.head[h].next = b;

        release(&bcache.lock[h]);

        release(&bcache.evict);

        acquiresleep(&b->lock);
        return b;
      }
    }

    release(&bcache.lock[k]);
  }

  release(&bcache.evict);

  panic("bget: no buffers");
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

  int h = b->blockno % NBUCKET;

  acquire(&bcache.lock[h]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[h].next;
    b->prev = &bcache.head[h];
    bcache.head[h].next->prev = b;
    bcache.head[h].next = b;
  }
  
  release(&bcache.lock[h]);
} 

void
bpin(struct buf *b) {
  int h = b->blockno % NBUCKET;
  acquire(&bcache.lock[h]);
  b->refcnt++;
  release(&bcache.lock[h]);
}

void
bunpin(struct buf *b) {
  int h = b->blockno % NBUCKET;
  acquire(&bcache.lock[h]);
  b->refcnt--;
  release(&bcache.lock[h]);
}


