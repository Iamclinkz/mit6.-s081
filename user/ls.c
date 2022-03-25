#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//传入的大概是例如"/root/bin"这种,然后返回的是"bin"
char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  //代码执行到这里,当前p指向最后一个'/'后面的一个字符
  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p; //如果传入的本身就没有"/",那么直接返回"bin"
  memmove(buf, p, strlen(p)); //把"bin"拷贝到buf中
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p)); //把字符串用空格填充,例如"bin",填充成"bin       "
  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de; //
  struct stat st;   //inode节点

  //打开文件,获取文件描述符
  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  //使用文件描述符获取inode节点,放到st中
  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  //判断inode节点类型
  switch(st.type){
  case T_FILE:
    //如果是文件类型,列出     名称            类型    inode节点号 大小
    printf("%s %d %d %l\n", fmtname(path), st.type, st.ino,    st.size);
    break;

  case T_DIR:
  //如果是目录类型
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    //把path拷贝到buf中
    strcpy(buf, path);
    //p指向buf中的path的最后一个位置
    p = buf+strlen(buf);
    //放一个"/",例如"root/bin" -> "root/bin/"
    *p++ = '/';
    //每次从文件描述符描述的目录文件中读出de大小的内容,用于赋值给de(也就是说其实目录文件中是很多个dirent数据结构的顺序存储)
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
      //如果inode节点大小无效,continue
        continue;
      //将文件的名称拷贝到例如""root/bin/"的后面,组成一个完整的字符串
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;  //为了输出,将DIRSIZ之后的最后一位设置成0,方便打印,例如当前文件为"ls",那么buf中已经组成了"root/bin/ls'\0'   '\0'"这样的字符串了
      if(stat(buf, &st) < 0){
        //通过名称获取其inode节点
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit(0);
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit(0);
}
