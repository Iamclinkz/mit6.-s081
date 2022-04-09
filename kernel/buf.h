struct buf {
  int valid;   // has data been read from disk?当前的buf是否装了某个block的copy
  int disk;    // does disk "own" buf? 缓冲区的内容是否已经写入磁盘
  uint dev;     //当前buf存放的是哪个设备的内容
  uint blockno; //当前buf存放的是某个设备的哪个block的内容
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

