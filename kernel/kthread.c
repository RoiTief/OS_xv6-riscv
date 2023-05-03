#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct proc proc[NPROC];

extern void forkret(void);

void 
kthreadinit(struct proc *p)
{
	initlock(&p->alloc_lock,"allock_lock");
  for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
  {
		initlock(&kt->lock,"kthread_lock");
		kt->state = K_UNUSED;
		kt->proc = p; 
    // WARNING: Don't change this line!
    // get the pointer to the kernel stack of the kthread
    kt->kstack = KSTACK((int)((p - proc) * NKT + (kt - p->kthread)));
  }
}

struct kthread*
mykthread()
{
  push_off();
	struct cpu *c = mycpu();
	struct kthread *kt = c->kthread;
	pop_off();
	return kt;
}

struct trapframe*
get_kthread_trapframe(struct proc *p, struct kthread *kt)
{
  return p->base_trapframes + ((int)(kt - p->kthread));
}

int
allocktid(struct proc *proc)
{
	int ktid;
	acquire(&proc->alloc_lock);
	ktid = proc->counter++;
	release(&proc->alloc_lock);
	return ktid;
}

struct kthread*
allockthread(struct proc* p)
{
	struct kthread *kt;
	for (kt = p->kthread; kt < &p->kthread[NKT]; kt++)
	{
		acquire(&kt->lock);
		if (kt->state != K_UNUSED)
			release(&kt->lock);
		else
		{
			kt->ktid = allocktid(p);
			kt->state = K_USED;
			kt->trapframe = get_kthread_trapframe(p, kt);
			memset(&kt->context, 0, sizeof(kt->context));
			kt->context.ra = (uint64)forkret;
			kt->context.sp = kt->kstack + PGSIZE;
			return kt;
		}
	}
	return 0;
}

void
freekthread(struct kthread *kthread)
{
  acquire(&kthread->lock);
  kthread->trapframe = 0;
  kthread->chan = 0;
  kthread->killed = 0;
  kthread->ktid = 0;
  kthread->state = K_UNUSED;
  release(&kthread->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&mykthread()->lock);
//   release(&mykthread()->proc->lock);
  

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

