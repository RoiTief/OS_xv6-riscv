#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/cfs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void int_to_char_array(uint64 n, char* str) {
				int i = 0;

				while(str[i] != 0) i++;

				// Generate digits in reverse order
				do {
								str[i++] = n % 10 + '0';
								n /= 10;
				} while (n > 0);

				str[i] = '.';
				str[i+1] = 't';
				str[i+2] = 'x';
				str[i+3] = 't';
				str[i+4] = 0;
}

void run_test(){
				struct cfs_data data;
				int id = getpid();
				char file_name[20];
				for(int i = 1; i <= 1000000000;i++){
								if(i%10000000 == 0){
												sleep(1);
								}
				}
				get_cfs_stats(id,&data);

				if (data.priority == HIGH) 
				{
								sleep(100);
								strcpy(file_name, "HIGH"); 
				}
				else if (data.priority==NORMAL) 
				{
								sleep(200);
								strcpy(file_name, "NORMAL");
				}
				else
				{
								sleep(300);
								strcpy(file_name, "LOW");
				}

				int_to_char_array(id,file_name);
				int fd = open(file_name, O_CREATE|O_WRONLY);
				if(fd < 0){
								printf("open(copyin1) failed\n");
								exit(1, "");
				}

				printf("id: %d priority: %s rtime: %l stime: %l retime: %l \n",id, data.priority==HIGH ? "HIGH" : data.priority==NORMAL ? "NORMAL" : "LOW", data.rtime, data.stime, data.retime,&data,&id);
				close(fd);
}

int main() {
				int num_prio0 = 1;
				int num_prio1 = 1;
				int num_prio2 = 1;
				int pids[num_prio0 + num_prio1 + num_prio2];
				char buf[32];
				int i = 0;
				for (int j = 0; j < num_prio0; j++) {
								if((pids[i++] = fork()) == 0){
												set_cfs_priority(0);
												run_test();
												exit(0, "high priority end");
								}
				}
				for (int j = 0; j < num_prio1; j++) {
								if((pids[i++] = fork()) == 0){
												set_cfs_priority(1);
												run_test();
												exit(0, "normal priority end");
								}
				}
				for (int j = 0; j < num_prio2; j++) {
								if((pids[i++] = fork()) == 0){
												set_cfs_priority(2);
												run_test();
												exit(0, "low priority end");
								}
				}
				for (i = 0; i < num_prio0 + num_prio1 + num_prio2; i++)
								wait(&pids[i], buf);
				exit(0, "end of cfs test\n");
}

