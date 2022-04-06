//
// low-level driver routines for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
#define Reg(reg) ((volatile unsigned char *)(UART0 + reg))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
//这里的设计是读指针追赶写指针,例如不考虑循环数组的情况下,一共有5个位置,当前读指针在idx = 2的位置
//写指针在idx = 4的位置,说明接下来应该读取2,3位置上的内容用于输出,如果有输入,应该放在4的位置上.
//即读指针追赶写指针的模型.从r指针到w指针中间的内容应该是要放到显示器上显示的内容.
int uart_tx_w; // write next to uart_tx_buf[uart_tx_w++]
int uart_tx_r; // read next from uart_tx_buf[uar_tx_r++]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&uart_tx_lock, "uart");
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);   //获取锁

  if(panicked){
    for(;;)
      ;
  }

  while(1){
    if(((uart_tx_w + 1) % UART_TX_BUF_SIZE) == uart_tx_r){
      //循环链表,如果写指针追上了读指针(位于读指针的上一个位置),那么说明当前buf满了
      //那么我们需要将uart_tx_lock释放掉,然后等待uart_tx_r的移动
      // buffer is full.
      // wait for uartstart() to open up space in the buffer.
      sleep(&uart_tx_r, &uart_tx_lock);
    } else {
      uart_tx_buf[uart_tx_w] = c;       //把c放到uart_tx_w的位置上
      uart_tx_w = (uart_tx_w + 1) % UART_TX_BUF_SIZE;   //循环链表移动一下写指针
      uartstart();      //start一手uart
      release(&uart_tx_lock);   //放锁
      return;
    }
  }
}

// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      //如果写指针==读指针,那么当前队列是空的,那么直接返回
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      //LSR_TX_IDLE == 1 说明当前THR不是满的,可以发送.如果THR是满的,直接return即可
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    //取出内容,然后改变读指针,然后发送
    int c = uart_tx_buf[uart_tx_r];
    uart_tx_r = (uart_tx_r + 1) % UART_TX_BUF_SIZE;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    //THR(Transmit Holding Register)是用于存放数据的,可以实际发送的FIFO,大概为32bits?可以写.
    //这里加锁可以保证同时只能有一个进程写这个寄存器.同时可以见上面几行的英文注释,如果LSR_TX_IDLE从0变成1,
    //那么会产生中断,这样比如我们cpu0当前在这个位置执行完WriteReg(THR, c);然后还有要读的内容,然后进行下一轮while
    //循环,再次走到第二个if,这时产生了一个中断,如果不加锁,那么uart的中断处理函数和我们这个函数会同时更改r指针,从而可能出错.
    //所以hold一下uart_tx_lock是必须的.
    WriteReg(THR, c);   
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from trap.c.
void
uartintr(void)
{
  // read and process incoming characters.
  //如果是RBR(aka RHR)满产生的,说明收到了新的内容,应该从RBR中读取输入,然后从控制台显示
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }

  // send buffered characters.
  //看看是不是因为THR空产生的,如果是的话应该及时把buf中的内容进行发送.注意这里要加锁,隔绝uartputc()
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
