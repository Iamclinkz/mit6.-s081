// Format of an ELF executable file

//ELF二进制文件以四字节的“幻数”0x7F、“E”、“L”、“F”或ELF_MAGIC开头
#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header,ELF头
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint64 entry;
  uint64 phoff;   //proghdr的偏移
  uint64 shoff;   
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum;     //proghdr的个数
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// Program section header,程序段头,每个proghdr描述了应用程序中必须加载到内存中的一部分
struct proghdr {
  uint32 type;    //proghdr的类型
  uint32 flags;
  uint64 off;   
  uint64 vaddr;   //这个段需要放置的逻辑地址的起始位置
  uint64 paddr;
  uint64 filesz;  //这个段实际的可读入的大小(memsz应该≥filesz,如果＞,那么用0填补)
  uint64 memsz;   //这个段需要分配的内存大小
  uint64 align;   //指示边界对齐
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
