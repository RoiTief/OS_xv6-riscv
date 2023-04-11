enum cfs_priority { HIGH, NORMAL, LOW};

struct cfs_data{
	enum cfs_priority priority;
	uint64 rtime; // run time
	uint64 stime; //sleep time
	uint64 retime; // runnable time
};

