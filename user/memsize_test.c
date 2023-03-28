#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
	void* p;
	printf("the size of the process now is : %d \nallocating 20k bytes\n", memsize());
	p =malloc(20000);
	printf("the size of the process now is : %d \nfreeing the pointer\n", memsize());
	free(p); 
	printf("the size of the process now is : %d\n", memsize());
	exit(0, "");
}
