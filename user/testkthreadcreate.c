#include "user.h"

void* func(){
    kthread_exit(0);
    return 0;
}

int main(int argc, char** argv){
    printf("This test will try to call for kthread_create\nExpected Behvaiour: the new thread's id should be 2.\nfor further testing, one should run this test while debugging allocproc after running ls.\nThis is in order to check that the main thread is the first thread in the kthread array\n");
    void* memory = malloc(4000);
    int tid = kthread_create(func,memory,4000);
    fprintf(2,"[main] created %d\n",tid);
    if(tid != 2){
        fprintf(2,"[ERROR] got tid=%d,should have been 1\n",tid);
        return 0x66;
    }
    kthread_exit(0);
    return 1;
}