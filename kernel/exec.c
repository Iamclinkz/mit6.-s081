#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

//使用从文件系统中存储的文件初始化地址空间的用户部分
int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    //从最上方读出elfhdr,放到elf中
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  //创建一个拥有trampoline和trapframe这两个映射的页表,放到pagetable中
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    //printf("exec[%s]:%d\n",path,i);
    //从elf.phoff开始,依次读入proghdr,xv6程序只有一个proghdr,也就是ELF_PROG_LOAD段
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
    //读出一个proghdr
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
    //如果该proghdr的类型不是ELF_PROG_LOAD类型,那么continue
    //只会加载ELF_PROG_LOAD段
      continue;
    if(ph.memsz < ph.filesz)
    //如果申请的mem size小于本段文本需要加载字节的大小,那么报错
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
    //检查总和是否溢出64位整数,防止恶意攻击
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
    //分配(sz, ph.vaddr + ph.memsz)这段虚拟内存大小的物理内存,并且将分配的物理内存加入页表
    //     sz是上一个段的顶部的位置
      goto bad;
    //迭代,这也说明每个elf初始的段是挨着的
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
    //检查是否对齐
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
    //将该段拷贝到刚刚分配的物理内存中
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  //这里可以看lab3.1的打印页表,顶级页表第0页的0,1,2号页表项分别记录了data段,stack段,guard page段
  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  //这是为了防止用户越界
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  //由于栈是从上往下的,所以这里要用减号
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  //设置栈的底部
  stackbase = sp - PGSIZE;

  //将栈设计的同p36的图一样
  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  //lab3.1
  if(p->pid==1) 
    vmprint(p->pagetable);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
  //检查va是否是页起始位置
    panic("loadseg: va must be page aligned");

  for(i = 0; i < sz; i += PGSIZE){
    //依次找到从va起始的逻辑地址的物理地址
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
    //n是复制的字节数
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip       ,                0,  (uint64)pa,   offset+i,           n) != n)
      //     inode指针 使用用户空间逻辑地址     物理地址   偏移量    复制的字节数
      return -1;
  }
  
  return 0;
}
