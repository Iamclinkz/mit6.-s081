#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// scratch area for timer interrupt, one per CPU.
uint64 mscratch0[NCPU * 32];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

//当在0x8000000上方,为每个cpu初始化一个向下生长的栈之后,跳转到此处执行
// entry.S jumps here in machine mode on stack0.
void
start()
{
  //见p27页倒数第三段,详细描述了start()的作用,主要是为切换到管理模式,跳转至main做铺垫
  //start执行一些仅在机器模式下允许的配置，然后切换到监控模式。
  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  //设置异常返回地址为main的地址
  w_mepc((uint64)main);

  // disable paging for now.
  //将0写入页表寄存器satp,禁用管理模式下的虚拟地址转换
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  //通过对时钟芯片编程,生成计时器中断.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  //使用rv提供的mret指令,进入监控模式.跳转至main
  asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void
timerinit()
{
  // each CPU has a separate source of timer interrupts.
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..3] : space for timervec to save registers.
  // scratch[4] : address of CLINT MTIMECMP register.
  // scratch[5] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &mscratch0[32 * id];
  scratch[4] = CLINT_MTIMECMP(id);
  scratch[5] = interval;
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  w_mie(r_mie() | MIE_MTIE);
}
