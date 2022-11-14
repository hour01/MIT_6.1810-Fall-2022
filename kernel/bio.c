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

#define NBUCKETS 13
struct {
  struct spinlock steal_lock;
  struct spinlock locks[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head of each buckets.
  struct buf hashbucket[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.steal_lock, "bcache_steal");
  for(int i=0;i<NBUCKETS;i++)
    initlock(&bcache.locks[i], "bcache");

  // Create linked list of buffers
  // init head of each buckets
  for(int i=0;i<NBUCKETS;i++)
  {
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }
  
  // initially, put all buff to the first bucket
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.hashbucket[0].next;
    b->prev = &bcache.hashbucket[0];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[0].next->prev = b;
    bcache.hashbucket[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = blockno%NBUCKETS;
  acquire(&bcache.locks[bucket]);

  // Is the block already cached?
  for(b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.hashbucket[bucket].prev; b != &bcache.hashbucket[bucket]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // no more accessible buf in this bucket, get from other bucket
  acquire(&bcache.steal_lock);
  for(int i=0; i<NBUCKETS; i++){
    if (i==bucket)
      continue;

    acquire(&bcache.locks[i]);

    // steal from the ith bucket
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev)
    {
      if(b->refcnt == 0) {

        // take out of other link
        b->next->prev = b->prev;
        b->prev->next = b->next;

        // add to this bucket link
        b->next = bcache.hashbucket[bucket].next;
        b->prev = &bcache.hashbucket[bucket];
        bcache.hashbucket[bucket].next->prev = b;
        bcache.hashbucket[bucket].next = b;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        release(&bcache.locks[bucket]);
        acquiresleep(&b->lock);
        release(&bcache.locks[i]);
        release(&bcache.steal_lock);
        return b;
      }
    }
    release(&bcache.locks[i]);
  }
  release(&bcache.steal_lock);


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

  int bucket = (b->blockno)%NBUCKETS;

  releasesleep(&b->lock);
  acquire(&bcache.locks[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[bucket].next;
    b->prev = &bcache.hashbucket[bucket];
    bcache.hashbucket[bucket].next->prev = b;
    bcache.hashbucket[bucket].next = b;
  }
  
  release(&bcache.locks[bucket]);
}

void
bpin(struct buf *b) {
  int bucket = (b->blockno)%NBUCKETS;
  acquire(&bcache.locks[bucket]);
  b->refcnt++;
  release(&bcache.locks[bucket]);
}

void
bunpin(struct buf *b) {
  int bucket = (b->blockno)%NBUCKETS;
  acquire(&bcache.locks[bucket]);
  b->refcnt--;
  release(&bcache.locks[bucket]);
}


