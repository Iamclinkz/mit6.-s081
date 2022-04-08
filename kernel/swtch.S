# Context switch
#
#   void swtch(struct context *old, struct context *new);
#   相当于是线程的切换,从第一个参数对应的线程切换到了第二个参数对应的线程之中
# Save current registers in old. Load from new.	

# 只需要保存部分寄存器的原因是,swtch函数可以认为和普通的c语言函数一样,并且执行完也正常
# 的返回调用点(只不过返回的是不同的线程),所以它跟普通的c语言函数一样有callee saved和caller saved寄存器的概念,
# 个人理解callee
# 所以swtch作为callee,只需要保存约定好的callee saved寄存器即可,其他的寄存器会被编译器产生代码,保存
# 到caller的栈中,结束之后再恢复.注意sp也跟着切换了,所以例如参数1对应线程a,参数2对应线程b,那么线程a
# 中的caller saved寄存器在执行本函数前被保存到了线程a的堆栈中,要等到下一次切换回线程a之后弹出,而线程b的callee saved
# 寄存器在执行结束本函数之后加载到cpu寄存器中,然后ret后再执行编译器产生的代码,从线程b的堆栈中弹出其余的
# caller saved寄存器,即可完成整个的调用链.实在是喵呀喵呀
.globl swtch
swtch:
        # 对于sched调用本函数而言,起作用是:
        # 将ra保存在了a0(本线程的内核线程的context地址)指向的地址的0偏移处,
        # 结合proc.h中的context结构可知存放的是ra字段.依次类推,保存本线程的内核线程
        # 的寄存器到本线程的context中,然后加载本cpu核的调度器线程的context到寄存器中
        sd ra, 0(a0)
        sd sp, 8(a0) # 注意保存了sp,相当于是切换了堆栈
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        # 这里ret,返回的是ra指向的地址,也就是第二个参数指向的context中保存的ra的地址
        # 代码执行到这里返回后,相当于是返回到了第二个参数对应的线程的上次中断点处继续执行
        # 对于从sched调用本函数而言,跳转到的是调度器线程的上次中断处,也就是scheduler函数中
        # 的swtch函数结束的位置(大概是proc.c中的c->proc = 0;这一句代码处)
        ret

	
