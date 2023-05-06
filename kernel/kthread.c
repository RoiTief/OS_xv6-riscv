#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct proc proc[NPROC];

extern void forkret(void);
extern struct spinlock wait_lock;



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

void
kill_kt(struct kthread* kt){
	acquire(&kt->lock);
	kt->killed =1;
	release(&kt->lock);
}

int
kthread_create(uint64 start_func, void *stack, uint stack_size){
	struct kthread *kt;	
	struct proc *p;
	p = myproc();

	acquire(&p->lock);
	kt = allockthread(p); // note that allockthread locks the kt, so we need to release in order
	release(&p->lock);

	if(kt){
		release(&p->lock); 
		return -1;
		}
	// allocated a new thread.
	// release the p lock so others can acquire.
	release(&kt->lock);
	release(&p->lock);

	
	acquire(&kt->lock);
	kt->state = K_RUNNABLE;

	// update trapframe 
	kt->trapframe->epc = (uint64) start_func;
	kt->trapframe->sp = ((uint64) stack) + stack_size;
	release(&kt->lock);
	

	return kt->ktid;
}

int
kthread_id(void){
	return mykthread()->ktid;
}

int
kthread_kill(int ktid){
	struct proc *p;
	p = myproc();
	int found = 0;

	
	if(mykthread()->ktid == ktid)
		return -1; // should fail, you could not kill yorself, please choose exit.
	
	acquire(&p->lock);
  for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT] && !found; kt++)
  {
		if(kt == mykthread())
			continue;

		acquire(&kt->lock);

		if(kt->ktid == ktid)
		{ 
			kt->killed = 1;
			found = 1;
			if(kt->state == K_SLEEPING)
			{
				kt->state = K_RUNNABLE;
			}
		}
		release(&kt->lock);    
  }
  release(&p->lock);
  return found ? 0 : -1; 
}


//proc lock should acquired when enter, return if there another active thread from the process
int
is_another_active_thread(){
	struct kthread* my_kt = mykthread();
	struct proc* p = myproc();
	
	for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT] ; kt++)
	{
		if(kt == my_kt)
			continue;
		
		acquire(&kt->lock);

		if(kt->state != K_ZOMBIE && kt->state != K_UNUSED)	{
			release(&kt->lock);
			return 1;
		}

		release(&kt->lock);
	}
	return 0;
}

int
is_kt_killed(struct kthread *kth)
{
  int killed;
  acquire(&kth->lock);
  killed = kth->killed;
  release(&kth->lock);
  return killed;
}

void
kthread_exit(int status){
	struct proc *p = myproc();
	int another_active_thread;
	struct kthread* kt = mykthread();
	
	acquire(&wait_lock);
  	acquire(&p->lock);
	another_active_thread = is_another_active_thread();

	if(another_active_thread){
		acquire(&kt->lock);
		kt->xstatus = status;
		kt->state = K_ZOMBIE;

		// release both lock so we can release p.
		release(&kt->lock); 
		release(&p->lock);

		wakeup(kt);
		acquire(&kt->lock);
		release(&wait_lock);

		sched();
	}
	else{
		release(&p->lock);
		release(&wait_lock);
		exit(status);
	}
	release(&p->lock);
}

// proc lock should not be acquired when you call this function
void kill_other_threads(){
	struct proc *p = myproc();
	struct kthread *my_kt = mykthread();

	acquire(&p->lock);
  for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
  {
	if(kt == my_kt)
		continue;
    
	acquire(&kt->lock);
	if(kt->state != K_UNUSED && kt->killed == 0)
		kt->killed = 1;

	if(kt->state == K_SLEEPING)
		kt->state = K_RUNNABLE;
	
	release(&kt->lock);
  }
	// none thread had been found
  release(&p->lock);
}

void
kill_all_other_and_wait(uint* k_status){
	struct kthread* my_kt = mykthread();
	struct proc* p = myproc();
	int thread_count;
	
	kill_other_threads();
	thread_count = get_kt_counter(p);
	
	for (int i = 1 ; i < thread_count ; i++){
		if(i != my_kt->ktid)
			kthread_join(i, (uint64)k_status);
	}
}

int kthread_join(int ktid, uint64 status)
{
  struct kthread *kt;
  struct proc *p;
  int found = 0;
  int output = 0;

  p = myproc();
  acquire(&wait_lock);

  if(mykthread()->ktid == ktid)
	return -1;

  for(;;){
    acquire(&p->lock);
    for(kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
    	if(mykthread() == kt || kt->ktid != ktid)
				continue;
      
	  // we are on the required kthread
	
			acquire(&kt->lock);
			if(kt->state != K_ZOMBIE){
				release(&kt->lock);
				found = 1;
				break; 
			}

			if(kt->state == K_ZOMBIE)
			{ // then the requested thread is done end status is waiting to be collected
				if(status != 0 && copyout(p->pagetable, status, (char *)&kt->xstatus,
										sizeof(kt->xstatus)) < 0) {
					output = -1;
				}
				kt->ktid = 0;
				kt->state = K_UNUSED;
				release(&kt->lock);
				release(&p->lock);
				release(&wait_lock);
				return output;
			}
		 
    }
    release(&p->lock);

    // if the requested ktid is not found, then return -1 
    if(!found || is_kt_killed(kt))
    {
      release(&wait_lock);
      return -1;
    }

    
    sleep(kt, &wait_lock);  
  }
}
