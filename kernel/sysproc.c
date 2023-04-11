#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  char exit_msg[32];
  argint(0, &n);
  argstr(1, exit_msg, 32);
  exit(n, exit_msg);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
	uint64 msg;
  argaddr(0, &p);
  argaddr(1, &msg);
  return wait(p, msg);
}

	uint64
sys_sbrk(void)
{
	uint64 addr;
	int n;

	argint(0, &n);
	addr = myproc()->sz;
	if(growproc(n) < 0)
		return -1;
	return addr;
}

	uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
 	if(killed(myproc())){
  		release(&tickslock);
  		return -1;
  	}
  	sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
	uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_memsize(void)
{
  return myproc()->sz;
}

uint64
sys_set_ps_priority(void)
{
	int n;

	argint(0, &n);
  set_ps_priority(n);
	return 0;
}

uint64
sys_set_cfs_priority(void)
{
	int n;

	argint(0, &n);
  set_cfs_priority(n);
	return 0;
}

uint64
sys_get_cfs_stats(void)
{
	int n;
	uint64 data;

	argint(0, &n);
	argaddr(1, &data);
  get_cfs_stats(n, (struct cfs_data*)data);
	return 0;
}

struct spinlock printf_lk;

uint64
sys_printf_init(void){
	initlock(&printf_lk, "uprintf");
	return 0;
}

uint64
sys_printf_acquire(void)
{
	acquire(&printf_lk);
	return 0;
}

uint64
sys_printf_release(void)
{
	release(&printf_lk);
	return 0;
}

