#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

static struct spinlock lock;
static char seed = 0x2A;

char 
lfsr_char(void)
{
	char bit;
	bit = ((seed >> 0) ^ (seed >> 2) ^ (seed >> 3) ^ (seed >> 4)) & 0x01;
	seed = (seed >> 1) | (bit << 7);
	return seed;
}


int
randomwrite(int user_src, uint64 src, int n)
{
	if (n != 1)
		return -1;
	
	acquire(&lock);

  if(either_copyin(&seed, user_src, src, 1) == -1) {
		release(&lock);
		return -1;
	}

	release(&lock);
  return 1;
}

int
randomread(int user_dst, uint64 dst, int n)
{
  uint target;

  target = n;
  acquire(&lock);
  while(n > 0){
    // copy the input byte to the user-space buffer.
		char random = lfsr_char();
    if(either_copyout(user_dst, dst, &random, 1) == -1)
      break;

    dst++;
    --n;
  }
  release(&lock);

  return target - n;
}

void
randominit(void)
{
  initlock(&lock, "rand");

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[RANDOM].read = randomread;
  devsw[RANDOM].write = randomwrite;
}
