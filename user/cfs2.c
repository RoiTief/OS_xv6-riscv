#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/cfs.h"
#include "user/user.h"

int
main(void)
{
				// child - priority 0
				if (fork() == 0){
								sleep(1);
								set_cfs_priority(0);
								for(int i = 0; i < 1000000000; i++){
												if(i % 100000000 == 0){
																sleep(10);
												}
								}
								struct cfs_data data;
								get_cfs_stats(getpid(), &data);
								sleep(100);
								printf("pid: %d, cfsPriority: %s, rtime: %d, stime: %d, retime: %d\n", getpid(), "HIGH", data.rtime, data.stime, data.retime);
								exit(0,"");
				}

				// child - priority 1
				if(fork() == 0){
								sleep(2);
								set_cfs_priority(1);
								for(int i = 0; i < 1000000000; i++){
												if(i % 100000000 == 0){
																sleep(10);
												}
								}
								struct cfs_data data;
								get_cfs_stats(getpid(), &data);
								sleep(100);
								printf("pid: %d, cfsPriority: %s, rtime: %d, stime: %d, retime: %d\n", getpid(), "NORMAL", data.rtime, data.stime, data.retime);
								exit(0, "");
				}

				// parent - priority 2
				set_cfs_priority(2); 
				sleep(3);
				for (int i = 0; i < 1000000000; i++){
								if (i % 100000000 == 0){
												sleep(10);
								}
				}

				struct cfs_data data;
				get_cfs_stats(getpid(), &data);
				sleep(100);
				printf("pid: %d, cfsPriority: %s, rtime: %d, stime: %d, retime: %d\n", getpid(), "LOW", data.rtime, data.stime, data.retime);
				wait(0, 0);
				wait(0, 0);
				exit(0,"");
}

