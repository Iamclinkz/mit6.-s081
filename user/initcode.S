# Initial process that execs /init.
# This code runs in user space.

//通过调用exec系统调用重新进入内核.exec替换了当前系统的内存和寄存器.
//它将返回/init进程中的用户空间。Init（user/Init.c:15）根据需要创建一个新的控制台设备文件，
//然后将其作为文件描述符0、1和2打开。然后它在控制台上启动一个shell。系统启动了。 
#include "syscall.h"

# exec(init, argv)
.globl start
start:
        la a0, init
        la a1, argv
        li a7, SYS_exec
        ecall

# for(;;) exit();
exit:
        li a7, SYS_exit
        ecall
        jal exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .long init
  .long 0
