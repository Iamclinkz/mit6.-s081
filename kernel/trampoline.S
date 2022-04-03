        # 内核在进入用户空间之前,会将STVEC寄存器的内容设置为trampoline的指针,
        # 那么如果我们在程序中请求系统调用(ecall指令)/外部中断/异常的时候,都会自动的将STVEC中的内容加载到pc中,
        # 从而跳转到trampoline执行.(注:ecall指令的作用还有设置当前mode为supervisor mode,保存pc到sepc中)
        # 然后trampoline再把用户的寄存器存到trapframe中,跳到内核中,这样就完成了整个的中断进入过程
        # code to switch between user and kernel space.
        #
        # this code is mapped at the same virtual address
        # (TRAMPOLINE) in user and kernel space so that
        # it continues to work when it switches page tables.
        # 注意上边这句话,trampoline页的逻辑地址在用户空间和内核空间是一致的
	#
	# kernel.ld causes this to be aligned
        # to a page boundary.
        #
	.section trampsec
.globl trampoline
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #
        # sscratch points to where the process's p->trapframe is
        # mapped into user space, at TRAPFRAME.
        #
        
	# swap a0 and sscratch
        # so that a0 is TRAPFRAME
        # 交换a0和sscratch,由于上一次从内核空间切换回用户空间的时候,内核会将SSCRATCH寄存器的内容
        # 设置为0x3fffffe000,也就是trapframe page的指针,所以我们在进入uservec的第一时间将a0和SSCRATCH进行交换,
        # 即起到了保护a0的作用,又起到了当前a0是trapframe的指针,剩下的寄存器可以依次的放入a0指向的位置
        csrrw a0, sscratch, a0

        # save the user registers in TRAPFRAME,注意都是以a0为偏移,存放到trapframe中的
        # trapframe中的详细结构可以看proc.h
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        # 这里没有存用户本来的a0,之后再存
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)

        # restore kernel stack pointer from p->trapframe->kernel_sp
        # 将当前进程的内核堆栈的地址从trapframe中取出,放到sp(stack pointer)中
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        # 将当前的cpu核心id从 p->trapframe->kernel_hartid 中取出来,放到tp寄存器中
        ld tp, 32(a0)

        # load the address of usertrap(), p->trapframe->kernel_trap
        # 把trap.c中的usertrap()的指针写入到t0中
        ld t0, 16(a0)

        # restore kernel page table from p->trapframe->kernel_satp
        # 把内核页表(包括内核页表指针,内核页表的V,U,W,R位等可以直接加载到SATP寄存器中的内容)
        # 从p->trapframe->kernel_satp中加载到t1中
        ld t1, 0(a0)
        # 交换SATP和t1寄存器.这里因为trampoline在用户页表和内核页表都被映射到同一个虚拟地址上了,所以可以继续运行不会崩溃
        csrw satp, t1
        sfence.vma zero, zero

        # a0 is no longer valid, since the kernel page
        # table does not specially map p->tf.

        # jump to usertrap(), which does not return
        # 跳转到trap.c中的usertrap()中
        # 注:代码执行到这里,当前寄存器组中有用的寄存器为:
        # sp:当前进程的内核堆栈; tp:当前的cpu核心id
        jr t0

.globl userret
userret:
        # userret(TRAPFRAME, pagetable)
        # switch from kernel to user.
        # usertrapret() calls here.
        # a0: TRAPFRAME, in user page table.
        # a1: user page table, for satp.

        # switch to the user page table.
        # 将页表从内核页表切换回用户页表
        csrw satp, a1
        sfence.vma zero, zero

        # put the saved user a0 in sscratch, so we
        # can swap it with our a0 (TRAPFRAME) in the last step.
        # 注意如果该中断是系统调用,那么代码执行到这里,trapframe中的a0实际上是syscall中的返回值了
        # (比如write()系统调用中的实际写入的字节数)从中取出,然后赋值给t0.如果是别的中断,那么trapframe中的a0仍然是
        # 用户原来的a0
        ld t0, 112(a0)
        # 然后再交换t0和sscratch,这样用户原本的a0就放到了sscratch中
        csrw sscratch, t0

        # restore all but a0 from TRAPFRAME
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0, and save TRAPFRAME in sscratch
        # 将a0和sscratch互换,注意再换之前,a0实际上保存了trapframe的在用户页表的虚拟地址,而sscratch中保存的
        # 是用户原本的a0,所以换一手正好
        csrrw a0, sscratch, a0
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        # 切换回user mode,sepc的值被重新的拷贝到pc中,开中断
        sret
