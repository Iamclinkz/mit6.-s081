// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

//lab6 
char kmemref[32730];

//getKmemrefIdx 把物理地址转换成kmemref的下标
int getKmemrefIdx(uint64 kmemAddr)
{
  return (kmemAddr - (uint64)end) / PGSIZE;
}

int getref(uint64 kmemAddr)
{
  return kmemref[getKmemrefIdx(kmemAddr)];
}

//addref 增加(当num为正时)/减少(num为负时)计数,返回更改之后的计数
int addref(uint64 kmemAddr,int num)
{
  int idx = getKmemrefIdx(kmemAddr);
  kmemref[idx] = kmemref[idx] + num;
  if(kmemref[idx]<0){
    panic("kmemref should not be negative");
  }else{
    return kmemref[idx];
  }
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
  //uint64 kend = (uint64)end;
  //printf("kend:%p,phystop:%p,sub:%p,num of pages:%d,rest:%d\n",kend,PHYSTOP,PHYSTOP-kend,(PHYSTOP-kend)/4096,(PHYSTOP-kend)%4096);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  //lab6 删除的时候减少引用计数,如果引用计数为0,才进行删除
  char ref = addref((uint64)pa,-1);
  if(ref != 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  //lab6
  if(addref((uint64)r,1)!=1){
    panic("kalloc:addref ret should be 1");
  }
  return (void*)r;
}
