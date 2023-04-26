#include "uthread.u"
#include "uthread_queue.h"
#include "user.h"
struct uthread threads[MAX_UTHREADS];
struct uthread *curr_thread;
struct ut_queue high_queue;
struct ut_queue medium_queue;
struct ut_queue low_queue;
int thread_num =0;
int started = 0;

struct uthread*
find_next_thread(void)
{	
	next_thread = q_size(&high_queue) > 0 ? q_poll(&high_queue) : 
									q_size(&medium_queue) > 0 ? q_poll(&medium_queue) :
									q_size(&low_queue) > 0 ? q_poll(&low_queue) : 0;
	
	if (!next_thread)
		exit(0);

	return next_thread;
}

void
switch_thread(struct uthread* next_thread){
	struct uthread* next_thread = find_next_thread();
	struct uthread* old_thread = curr_thread;
	curr_thread = next_thread;
	curr_thread->tstate = RUNNING;
	uswtch(&old_thread->context,&next_thread->context);
}

void
add_to_queue(struct uthread* t)
{
	t->priority == LOW ? q_add(&low_queue,t) : 
	t->priority == MEDIUM ? q_add(&medium_queue,t) :  
	q_add(&high_queue,t);
}

int 
uthread_create(void (*start_func)(), enum sched_priority priority)
{
	if(thread_num == MAX_UTHREADS)
		return -1;

	struct uthread* t;	
	for (t = threads ; t->tstate != FREE; t++);
	t->tstate = RUNNABLE;
	t->priority = priority
	t->context.ra = start_func;
	t->context.sp = t->stack // this is char[]
	thread_num++;
	add_to_queue(t);
	return 0;
}

void 
uthread_yield(){
	curr->tstate = RUNNABLE;
	add_to_queue(curr);
	switch_threads(next);
}

void 
uthread_exit(){
	curr->tstate = FREE;
	switch_threads(next);
}

int 
uthread_start_all(){
	if(started)
		return -1;
	started = 1;
	struct context c;
	struct uthread *to_run = find_next_thread();
	if (to_run)
	{
		to_run->tstate = RUNNING;
		uswtch(&c,&to_run->context);
	}
	return 0;
}

enum sched_priority uthread_set_priority(enum sched_priority priority){
	enum sched_priority previous = curr-thread->priority;
	curr-thread->priority =  priority;
	return previous;
}
enum sched_priority uthread_get_priority(){
	return curr-thread->priority;
}

struct uthread* uthread_self(){
	return curr_thread;
}

