#include "user/uthread.h"
#include "user/uthread_queue.h"
#include "user/user.h"

struct uthread THREADS[MAX_UTHREADS];
struct uthread *curr_thread;
struct ut_queue queues[3];
int started = 0;

struct uthread*
find_next_thread(void)
{	
	struct uthread *next_thread = 0;  
	next_thread =
									q_size(&queues[HIGH]) > 0 ? q_poll(&queues[HIGH]) : 
									q_size(&queues[MEDIUM]) > 0 ? q_poll(&queues[MEDIUM]) :
									q_size(&queues[LOW]) > 0 ? q_poll(&queues[LOW]) : 
									0;
	
	if (!next_thread)
	{
		kthread_exit(0);
	}

	return next_thread;
}

void
switch_thread(){
	struct uthread* next_thread = find_next_thread();
	struct uthread* old_thread = curr_thread;
	curr_thread = next_thread;
	curr_thread->state = RUNNING;
	uswtch(&old_thread->context,&next_thread->context);
}

int 
uthread_create(void (*start_func)(), enum sched_priority priority)
{
  struct uthread *t;
  for(t = THREADS; t->state != FREE && t < &THREADS[MAX_UTHREADS]; t++) continue;
	if (t >= &THREADS[MAX_UTHREADS]) return -1;
	t->priority = priority;
	memset(&t->context, 0, sizeof(t->context));
	t->context.ra = (uint64) start_func;
	t->context.sp = (uint64) t->ustack + STACK_SIZE;
	t->state = RUNNABLE;
	q_add(&queues[t->priority],t);
	return 0;
}

void 
uthread_yield()
{
  curr_thread->state = RUNNABLE;
	q_add(&queues[curr_thread->priority],curr_thread);
  switch_thread();
}

void 
uthread_exit()
{
	curr_thread->state = FREE; 
	switch_thread();
}

int 
uthread_start_all()
{
	if(started != 0)
		return -1;
	started = 1;
	struct context c;
	curr_thread = find_next_thread();
  curr_thread->state = RUNNING;
  uswtch(&c, &curr_thread->context);
	return -1;
}

enum sched_priority uthread_set_priority(enum sched_priority priority)
{
	enum sched_priority previous = curr_thread->priority;
	curr_thread->priority = priority;
	return previous;
}

enum sched_priority uthread_get_priority()
{
	return curr_thread->priority;
}

struct uthread* uthread_self()
{
	return curr_thread;
}

