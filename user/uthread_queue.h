#define MAX_QUEUE_SIZE = 10

struct uthread;

struct ut_queue;

int q_size(struct ut_queue *queue);

struct uthread* q_poll(struct ut_queue *queue);

int q_add(struct ut_queue *queue, struct uthread *to_add);

	
