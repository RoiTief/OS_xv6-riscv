#include "uthread_queue.h"
#include "uthread.h"
#include "user.h"


void 
q_init(struct ut_queue *queue)
{
	queue->size = 0;
	for (int i = 0; i < MAX_QUEUE_SIZE ; i++)
		queue->queue[i] = 0;
}

int 
q_size(struct ut_queue *queue)
{
	return queue->size;
}

struct uthread* 
q_poll(struct ut_queue *queue)
{
	struct uthread* to_return = 0;
	if (queue->size == 0)
		return to_return;
	int i = 0;
	for (; queue->queue[i] == 0 && i < MAX_QUEUE_SIZE; i++);
	to_return = queue->queue[i];
	queue->queue[i] = 0;
	queue->size--;
	return to_return;
}

int 
q_add(struct ut_queue *queue, struct uthread *to_add)
{
	if (queue->size == MAX_QUEUE_SIZE)
		return 0;
	
	//shift all objects in queue to empty the last spot
	for (int i = 0; i < (MAX_QUEUE_SIZE - 1) ; i++)
		queue->queue[i] = queue->queue[i + 1];
	
	queue->queue[MAX_QUEUE_SIZE-1] = to_add;
	queue->size++;
	return 1;
}
	
