#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}


//lab4.3,将当前的trapframe进行保存,保存至p->tickTrapframe中
void
tickSave()
{
  struct proc *p = myproc();
  memmove(&p->tickTrapframe,p->trapframe,sizeof(p->tickTrapframe));
}



//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
//代码调用到usertrap()时有用的两个寄存器:
//sp:当前进程的内核堆栈; tp:当前的cpu核心id
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    //检查中断发生时是否是来自用户mode,如果不是的话报错
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  //这里就要更改stvec寄存器了.对于用户态来说,如果发生中断,那么进入的是用户态的stvec寄存器指向的
  //trampoline的位置,而对于内核态来说,如果发生中断,那么进入的应该是kernelvec所指向的位置了.
  //所以这里应该更新stvec的值,让其指向kernelvec函数
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  //如果在内核直接切换到了别的进程,那么在别的进程运行时可能又有别的中断发生,这时候存放在sepc中的当前用户的pc就会被冲走(换成新的
  //进程在执行时被中断的pc).那么肯定是狗带的,所以需要保存当前被中断的用户进程的pc到本p的trapframe中
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    //scause寄存器中存放的是进入usertrap的原因,也就是保存了类似中断号?如果中断号是8的话,说明是系统调用
    // system call

    if(p->killed)
      //检查当前进程是否被其他进程杀死,如果被杀死,则不执行系统调用
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    //对于例如缺页这种,应该让pc不动,重新执行这一条指令,而对于系统调用来说,应该让pc+4,这样指向下一条指令,而不是让
    //pc保持不变,重新执行一遍ecall指令
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    //调用syscall()函数,位于syscall.c中
    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  //执行完系统调用/balabala以后,重新检查进程是否被杀掉了
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    //lab4.3
    if(p->ticks && !p->ticking){
      //如果当前进程设置了定时事件
      if(p->currentTicks){
        p->currentTicks--;
      }else{
        p->ticking = 1;
        tickSave();
        p->currentTicks = p->ticks;
        p->trapframe->epc = (uint64)p->handler;
        usertrapret();
      }
    }
    yield();
  }

  //由于是从汇编代码中直接jump指令而不是call指令过来的,所以usertrap()结束后并不返回uservec中,而是调用usertrapret返回
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // 由于我们又要返回用户空间了,所以又要将stvec改成指向trampoline.S中的uservec了
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  //为了下一个用户进程进入trampoline使用.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  //sstatus这个寄存器的SPP bit位控制了sret指令的行为，该bit为0表示下次执行sret的时候，我们想要返回user mode而不是supervisor mode。
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  //这个寄存器的SPIE bit位控制了，在执行完sret之后，是否打开中断。
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  //我们会把新的值写回到SSTATUS寄存器。
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  //把保存在trapframe中的epc重新放到epc寄存器中,因为当前一直是关着中断的,所以不怕有中断来冲走epc寄存器了
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  //satp中存放应该切换哪张页表(这里是用户页表).注意只能在trampoline中完成内核页表和用户页表的切换,因为只有在
  //trampoline中两张页表的虚拟地址是统一的,所以直接切换不会产生任何的问题
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  //计算一手userret的地址,放到fn中,执行fn,并且传入两个参数,这样两个参数(用户页表中TRAPFRAME的虚拟地址,返回的时候应该使用的页表)
  //就可以分别放到a0和a1中了,这样可以方便userret函数使用
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

