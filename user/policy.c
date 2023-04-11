#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char*argv[]){
	if (argc < 2)
		exit (-1,"error no given priority");
	int policy_code = argv[1][0] - '0';
	if (set_policy(policy_code) == 0)
		exit(0, "Policy set successfully");
	exit(1, "Error setting policy");

}

