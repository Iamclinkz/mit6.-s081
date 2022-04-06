// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held? 最关键的字段,表明当前的锁是不是lock状态

  //主要是为了调试使用,例如检测死锁等:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

