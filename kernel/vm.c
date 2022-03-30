#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.即内核页表的根页表
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// lab3.2 初始化用户的内核页表,初始化用户自己的内核态下的页表
pagetable_t userKernelPageTableInit()
{
  //为用户的内核页表创建一个顶级页表
  pagetable_t userKernelPageTable = (pagetable_t)uvmcreate();
  if(userKernelPageTable == 0){
    return 0;
  }
  
  kvmMapUserKernelPage(userKernelPageTable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmMapUserKernelPage(userKernelPageTable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmMapUserKernelPage(userKernelPageTable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  kvmMapUserKernelPage(userKernelPageTable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  //映射从内核开始到内核结束这段内核空间
  kvmMapUserKernelPage(userKernelPageTable, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  //映射从内核结束到PHYSTOP这段空间,因为是1->1映射的,所以相当于内核页表中,逻辑地址的最大值也是PHYSTOP
  kvmMapUserKernelPage(userKernelPageTable, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  //映射TRAMPOLINE
  kvmMapUserKernelPage(userKernelPageTable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return userKernelPageTable;
}
/*
 * 被main函数调用
 * create a direct-map page table for the kernel.
 */
void kvminit()
{
  //安排一页保存根页表页
  kernel_pagetable = (pagetable_t)kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  //注意以下都是直接映射,即物理地址和逻辑地址是相同的
  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  //映射从内核开始到内核结束这段内核空间
  kvmmap(KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  //映射从内核结束到PHYSTOP这段空间,因为是1->1映射的,所以相当于内核页表中,逻辑地址的最大值也是PHYSTOP
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  //映射TRAMPOLINE
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

//模拟rv的硬件使用页表翻译虚拟地址的过程,通过一个虚拟地址,找到对应的页表项
//如果alloc为1,那么在遍历的过程中如果索引页表的页表无效,那么会分配一个次级页表
//如果alloc为0那么不分配,如果找的过程中索引页表的页表对应的页表项
//是无效的话,直接返回-1.最终返回的是-1/一个实际映射到有效页(而非用于索引页表)的页表项的地址.
//这个页表项可能有效,也可能无效.
// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  //参数pagetable是顶级页表地址
  // va是64位的虚拟地址
  // alloc是指定如果该虚拟地址对应的页表没有被分配,那么进行分配
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    //进行两次间接索引,从树状结构逐级查找下一级的页表项
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V)
    {
      //如果该级页表项的PTE_V位为1,说明有效
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      //如果没有查到有效的页表项,并且alloc为0,那么直接返回0.不分配
      //如果alloc为1,但是kalloc分配不了,同样返回0
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      //将该页置为0,并且设置有效位
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  //注意代码执行到这里,该页表项的上一级页表一定是有效的,但是
  //本页表项有效/无效不一定
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
//只能用于遍历用户页表,进行逻辑地址到物理地址的映射
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// lab3.2自己加的用于初始化用户的内核页表
void kvmMapUserKernelPage(pagetable_t pg, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(pg, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  // lab3.2,改成用用户的内核页表的版本
  // pte = walk(kernel_pagetable, va, 0);

  pte = walk(myproc()->kernelPageTable, va, 0);
  if (pte == 0)
    panic("kvmpa");
  if ((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa + off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
//通过添加页表项的方式,将虚拟地址和物理地址通过页表进行映射,不会实际的分配物理内存
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);               // a记录了va的页的起始地址
  last = PGROUNDDOWN(va + size - 1); // last记录了va的页的结束地址
  for (;;)
  {
    //如果walk返回0,说明没法分配次级页表(系统错误),返回-1,如果返回的不是1,那么返回的是一个指向页表项的指针
    //可以将这个页表项进行初始化
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      //检查有效性,因为walk返回的页表项可能有效也可能无效,
      //如果有效,那么报错.
      panic("remap");
    //初始化页表值
    //物理地址    uxrw值  设置可用
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
//将pagetable的leave(也就是最底层页表的真正指向物理页的页表项)的值改为0,也就是指向的物理地址改为0,
//然后PTE_V,RWX也是0.并且如果do_free是1,释放掉该物理内存页.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    //判断是否边界对齐
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    //从va开始,一直走npages个页
    if ((pte = walk(pagetable, a, 0)) == 0)
      //判断这个页的高级页表的页表项是否有效,注意walk的alloc位为0,如果没有分配页表,那么直接返回0
      panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0)
      //判断该页的页表项是否有效
      panic("uvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if (do_free)
    {
      //如果do_free设置为1,那么顺便也调用kfree,把物理内存给释放了
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
//创建一个空的页表,并且将页表物理地址返回
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
//为第一个进程(init进程)分配一个物理页,然后将物理页映射到逻辑地址0的位置,然后将
// init的代码复制到mem的位置上(相当于复制到了逻辑地址0的位置上)
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
//从用户的虚拟地址oldsz一直到newsz之间使用kalloc分配整页整页的内存,并且将其加入对应进程的页表中
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    //如果新地址不如旧地址高,直接返回旧地址
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    //从新地址到旧地址,每次大小为一页
    mem = kalloc(); //分配一个新的物理内存
    if (mem == 0)
    {
      //如果内存不够,return
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    //初始化这个物理页的所有东西为0
    memset(mem, 0, PGSIZE);

    //将这个申请到的物理页进行映射,从虚拟地址a -> 物理地址men,并且设置对应的标志位
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
    {
      //如果分配不成功,那么uvmdealloc
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
//从oldsz到newsz(如果oldsz比newsz大才有效)之间的内存进行归还,并且
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    //计算从newsz到oldsz一共是多少页
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    //使用uvmunmap进行释放,do_free是1,说明同时执行物理内存的归还
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

//专门为了释放用户的内核页表而设计的freewalk.不用检查leave的PTE_V,直接释放所有的页表.
//因为freewalk之所以要检查leave的PTE_V,是因为如果贸然删除一个页表项,如果该页表项是唯一的关联该物理地址的页表项,那么
//其对应的物理地址可能永远都找不到了.但是我们释放用户内核页表的时候,由于所有的用户的内核页表中的leave对应的物理页都
//不只有本页表指向(同时被全局内核页表指向/用户页表指向).所以我们可以放心的不判断leave的PTE_V,直接删除页表,而如果想
//释放物理页,利用其他的页表删除即可.
void freewalkWithoutCheck(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalkWithoutCheck((pagetable_t)child);
      pagetable[i] = 0;
    }
  }
  kfree((void *)pagetable);
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
//递归的释放一个页表所占用的物理内存.注意这个页表所指向的物理内存必须在调用此函数之前全部释放完成
//否则将会没法寻址
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// lab3.3 用于fork()的时候拷贝User的用户页表,到用户的内核页表
//跟uvmcopy()不同的是,只为页表分配物理页,而不为拷贝用户的物理页分配物理页.
//将用户逻辑地址从from开始,sz大小的逻辑地址对应的页表转换以后加入到用户的内核页表中
//例如用户页表中有        [虚拟地址:物理地址] = [0x1000:0x20000]
//那么创建用户的内核页表   [虚拟地址:物理地址] = [0x1000:0x20000]
int copyUPT2UKPT(pagetable_t upt, pagetable_t ukpt,uint64 from, uint64 sz)
{
  pte_t *upte;
  pte_t *ukpte;
  
  if(sz < 0 || from + sz > PLIC){
    printf("copyUPT2UKPT err,sz:%d,from+sz:%d\n",sz,from+sz);
    return -1;
  }

  int i;
  for (i = from; i < from + sz; i += PGSIZE)
  {
    if ((upte = walk(upt, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    //为了考虑growproc中n是负的的情况,如果当前的用户页因为收缩而被删掉,那么
    //PTE_V位为0,那么我们应该也让内核的这一位为0,所以不用判断PTE_V为为0的情况.
    if ((ukpte = walk(ukpt, i, 1)) == 0)
    {
      panic("uvmcopy: can not alloc pt");
    }
    *ukpte = (*upte) & (~PTE_U);
  }
  return 0;
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  // uint64 n, va0, pa0;

  // while (len > 0)
  // {
  //   va0 = PGROUNDDOWN(srcva);
  //   pa0 = walkaddr(pagetable, va0);
  //   if (pa0 == 0)
  //     return -1;
  //   n = PGSIZE - (srcva - va0);
  //   if (n > len)
  //     n = len;
  //   memmove(dst, (void *)(pa0 + (srcva - va0)), n);

  //   len -= n;
  //   dst += n;
  //   srcva = va0 + PGSIZE;
  // }
  // return 0;
  //lab3.3
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  // uint64 n, va0, pa0;
  // int got_null = 0;

  // while (got_null == 0 && max > 0)
  // {
  //   va0 = PGROUNDDOWN(srcva);
  //   pa0 = walkaddr(pagetable, va0);
  //   if (pa0 == 0)
  //     return -1;
  //   n = PGSIZE - (srcva - va0);
  //   if (n > max)
  //     n = max;

  //   char *p = (char *)(pa0 + (srcva - va0));
  //   while (n > 0)
  //   {
  //     if (*p == '\0')
  //     {
  //       *dst = '\0';
  //       got_null = 1;
  //       break;
  //     }
  //     else
  //     {
  //       *dst = *p;
  //     }
  //     --n;
  //     --max;
  //     p++;
  //     dst++;
  //   }

  //   srcva = va0 + PGSIZE;
  // }
  // if (got_null)
  // {
  //   return 0;
  // }
  // else
  // {
  //   return -1;
  // }

  // lab3.3
  return copyinstr_new(pagetable, dst, srcva, max);
}

void doVmPrint(pagetable_t pt, int layer)
{
  //对于给定的页表,遍历其0~511个页表项
  for (int i = 0; i < 512; i++)
  {
    //取出页表项
    pte_t pte = pt[i];
    if (pte & PTE_V)
    {
      //如果该页表项正在使用
      if (layer == 3)
      {
        char *prefix = ".. .. ..";
        //如果到了叶子节点,不递归了,直接输出
        printf("%s%d: pte %p pa %p\n", prefix, i, pte, PTE2PA(pte));
        continue;
      }
      else
      {
        char *prefix;
        //如果没有到叶子节点,应该递归一手
        if (layer == 1)
          prefix = "..";
        else
          prefix = ".. ..";

        printf("%s%d: pte %p pa %p\n", prefix, i, pte, PTE2PA(pte));
        //获取其存放的页表的物理地址,记为child
        uint64 child = PTE2PA(pte);
        doVmPrint((pagetable_t)child, layer + 1);
      }
    }
  }
}

void vmprint(pagetable_t pt)
{
  printf("page table %p\n", pt);
  doVmPrint(pt, 1);
}
