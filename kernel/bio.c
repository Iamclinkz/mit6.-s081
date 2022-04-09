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
#define BucketNum 13

struct{
  struct spinlock lock;
  struct buf buf[NBUF];
}bcache[BucketNum];


//初始化bcachehash
void
binit(void)
{
  int i,j;
  for(i = 0;i!=BucketNum;i++){
    //初始化bcache的锁
    initlock(&bcache[i].lock, "bcache");
    for(j = 0;j!=NBUF;j++)
      initsleeplock(&bcache[i].buf[j].lock, "bcache");
  }
}

int getbucketid(uint dev,uint blockno){
  return (dev + blockno) % BucketNum;
}

static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bid = getbucketid(dev,blockno);
  acquire(&bcache[bid].lock);

  // Is the block already cached?
  //首先从cache中找这个块
  for(b = bcache[bid].buf; b != bcache[bid].buf + NBUF; b++){
    if(b->dev == dev && b->blockno == blockno){
      //如果从buf中找到了,直接返回
      b->refcnt++;
      release(&bcache[bid].lock);
      acquiresleep(&b->lock); //注意return b之前需要先等待该块被释放
      return b;
    }
  }

  //如果没找到,应该分配一手
  uint mintik = -1; //mintik设置为最大
  struct buf *dog = 0;  //应该被淘汰的块
  for(b = bcache[bid].buf; b != bcache[bid].buf + NBUF; b++){
    if(b->refcnt == 0) {
      if(mintik > b->tks){
        dog = b;
        mintik = b->tks;
      }
    }
  }
  if(mintik == -1)
    panic("bget: no buffers");
  
  dog->dev = dev;
  dog->blockno = blockno;
  dog->valid = 0;   //这里的valid暂时设置成0,然后让上层从磁盘中读取一手,这样可以避免使用上次cache中的内容
  dog->refcnt = 1;
  release(&bcache[bid].lock);
  acquiresleep(&dog->lock);
  return dog;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);   //调用bget获取一个cache
  if(!b->valid) {
    //如果b中没有装入设备号为dev,block号为blockno的内容(valid位为0),那么从磁盘中读取一手
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
//释放b的睡眠锁,并且检测b当前的ref情况,如果没有被其他进程同时使用,那么将b放到链表的首位
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  b->tks = ticks;
  b->refcnt--;
}

void
bpin(struct buf *b) {
  uint bid = getbucketid(b->dev,b->blockno);
  acquire(&bcache[bid].lock);
  b->refcnt++;
  release(&bcache[bid].lock);
}

void
bunpin(struct buf *b) {
  uint bid = getbucketid(b->dev,b->blockno);
  acquire(&bcache[bid].lock);
  b->refcnt--;
  release(&bcache[bid].lock);
}


