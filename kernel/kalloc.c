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

struct{
  struct spinlock lock;
  struct run *freelist;
  char lockname[8];
}kmemcpu[NCPU];


struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void initcpulock()
{
  int i;
  for(i=0;i!=NCPU;i++){
    //初始化kmemcpu
    snprintf(kmemcpu[i].lockname, 7, "kmem-%d", i);
    initlock(&(kmemcpu[i].lock), kmemcpu[i].lockname);
    printf("finish init %s\n",kmemcpu[i].lockname);
  }
}


void
kinit()
{
  //initlock(&kmem.lock, "kmem");
  initcpulock();
  freerange(end, (void*)PHYSTOP);
}

void
kfreecpu(void *pa,int myid){
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmemcpu[myid].lock);
  r->next = kmemcpu[myid].freelist;
  kmemcpu[myid].freelist = r;
  release(&kmemcpu[myid].lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  push_off();
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    //kfree(p);
    kfreecpu(p,cpuid());
  pop_off();
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  // struct run *r;

  // if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
  //   panic("kfree");

  // // Fill with junk to catch dangling refs.
  // memset(pa, 1, PGSIZE);

  // r = (struct run*)pa;

  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);
  push_off();
  kfreecpu(pa,cpuid());
  pop_off();
}



// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  //这里如果不加锁在多核race的情况下可能出现物理页丢失的错误.
  //里如果cpu0和cpu1同时执行了r = kmem.freelist;这句,然后执行kmem.freelist = r->next;先执行的那个cpu所
  //释放的物理内存块将会永久的丢失.可以画图试试
  // acquire(&kmem.lock);
  // r = kmem.freelist;
  // if(r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);

  // if(r)
  //   memset((char*)r, 5, PGSIZE); // fill with junk
  // return (void*)r;
  push_off();
  int myid = cpuid();
  acquire(&kmemcpu[myid].lock);
  r = kmemcpu[myid].freelist;
  if(r){
    //如果从本cpu的空闲链表中可以分配,那么直接分配
    kmemcpu[myid].freelist = r->next;
    release(&kmemcpu[myid].lock);
  }else{
    //如果本cpu的空闲链表中没有东西,那么从其他链表中分配
    release(&kmemcpu[myid].lock);
    for(int i=0;i!=NCPU;i++){
      if(i == myid){
        continue;
      }else{
        acquire(&kmemcpu[i].lock);
        r = kmemcpu[i].freelist;
        if(!r){
          //该cpu也没有可用的
          release(&kmemcpu[i].lock);
        }else{
          kmemcpu[i].freelist = r->next;
          release(&kmemcpu[i].lock);
          break;
        }
      }
    }
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
