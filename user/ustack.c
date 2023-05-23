#include "kernel/types.h"
#include "ustack.h"
#include "kernel/riscv.h"
#include "user.h"



struct header{
	struct header* prev;
	int allocated;
};

typedef struct header Header;

Header *head = 0;

int 
alloc_new_page(void)
{
	char *p;
	Header* new_head;
	
	p = sbrk(PGSIZE);
	if(p == (char*)-1)
	    return -1;
	new_head = (Header*)p;
	new_head->prev = head;
	new_head->allocated = sizeof(Header);
	head = new_head;
	return 1;	
}

void*
ustack_malloc(uint len)
{
	void* buffer;

	if (len <= 0 || len > 512)
		return (void*)-1;

	if(!head || (PGSIZE - head->allocated) < (len + sizeof(int))) 
	{
		if (alloc_new_page() == -1)
			return (void*)-1;
	}

	buffer = ((((void*)head)) + (head->allocated));

	head->allocated += len;

	*((int*)((void*)head + head->allocated)) = len; 

	head->allocated += sizeof(int);
	
	return buffer;
}

int 
ustack_free(void)
{
	int len;
	Header *prev;
	
	if(!head)
		return -1;

	len = *((int*)((void*)head + head->allocated - sizeof(int)));

	head->allocated -= (len + sizeof(int));

	if(head->allocated == sizeof(Header)){
		prev = head->prev;
		sbrk(-PGSIZE);
		head = prev;
	}	

	return len;
}

