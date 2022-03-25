// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  //中断一手
  push_off(); // disable interrupts to avoid deadlock.

  //如果当前已经获得了锁,panic
  if(holding(lk))
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;//自旋一手

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
//用栈来模拟了关中断和开中断的操作.上面的话细品.

//相当于关中断操作
void
push_off(void)
{
  int old = intr_get();     //获取当前是否中断

  intr_off();             //直接改标志寄存器的值,实际的执行关设备中断
  if(mycpu()->noff == 0)
  //如果当前cpu没有记录中断,那么将其置为中断进行中
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

//相当于开中断操作
void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    //如果当前开着中断,执行关中断操作的话会panic
    panic("pop_off - interruptible");
  if(c->noff < 1)
    //如果当前cpu没有记录正在中断中,panic
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    //如果当前栈中的中断请求都pop_off了,并且在第一个中断之前中断是开着的,那么重新开中断
    intr_on();
}
