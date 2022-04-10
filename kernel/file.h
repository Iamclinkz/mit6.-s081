struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  // copy of disk inode
  short type;         //是文件还是目录
  short major;
  short minor;
  short nlink;        //有多少文件名指向当前的inode
  uint size;          //文件数据有多少个字节
  //0-11保存的是direct block number,也就是直接索引,第12块指向的是一个块存放了256个直接索引的块,即保存的是间接索引
  //所以xv6中的文件的最大尺寸是(256+12)*1024字节
  uint addrs[NDIRECT+1];
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
