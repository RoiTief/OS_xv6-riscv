#include "uthread.h"
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
	struct uthread *next_thread = 
									q_size(&high_queue) > 0 ? q_poll(&high_queue) : 
									q_size(&medium_queue) > 0 ? q_poll(&medium_queue) :
									q_size(&low_queue) > 0 ? q_poll(&low_queue) : 
									0;
	
	if (!next_thread)
		exit(0);

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
	for (t = threads ; t->state != FREE; t++);
	t->state = RUNNABLE;
	t->priority = priority;
	t->context.ra = (uint64) start_func;
	t->context.sp = (uint64) &t->ustack + STACK_SIZE;
	thread_num++;
	add_to_queue(t);
	return 0;
}

void 
uthread_yield(){
	curr_thread->state = RUNNABLE;
	add_to_queue(curr_thread);
	switch_thread();
}

void 
uthread_exit(){
	curr_thread->state = FREE;
	switch_thread();
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
		to_run->state = RUNNING;
		curr_thread = to_run;
		uswtch(&c,&to_run->context);
	}
	return 0;
}

enum sched_priority uthread_set_priority(enum sched_priority priority){
	enum sched_priority previous = curr_thread->priority;
	curr_thread->priority =  priority;
	return previous;
}
enum sched_priority uthread_get_priority(){
	return curr_thread->priority;
}

struct uthread* uthread_self(){
	return curr_thread;
}

