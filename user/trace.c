#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  //输入例如:trace 32 grep hello README
  // argv[0]:trace
  // argv[1]:32
  // argv[2]:grep
  // argv[3]:hello
  // argv[4]:README

  int i;
  char *nargv[MAXARG];

  if (argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9'))
  {
    //校验第二个参数是否合法
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  //需要添加trace系统调用
  if (trace(atoi(argv[1])) < 0)
  {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }

  //复制一手从第三个参数开始的命令,执行
  for (i = 2; i < argc && i < MAXARG; i++)
  {
    nargv[i - 2] = argv[i];
  }
  exec(nargv[0], nargv);
  exit(0);
}
