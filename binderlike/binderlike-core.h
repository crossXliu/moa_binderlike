#ifndef __BINDERLIKE_CORE_H__
#define __BINDERLIKE_CORE_H__

#include <linux/io.h>

#define BINDERLIKE_INPUT_PARAM_MAX 6
#define BINDERLIKE_CHAN_MAX 16

struct moa_binderlike_msg {
	char content[256];
};

struct moa_binderlike_queue_cap {
	int sq_offset;
	int cq_offset;
	int memblk_size;
};

struct moa_binderlike_arg_table {
	unsigned int argc;
	unsigned int arg_size[BINDERLIKE_INPUT_PARAM_MAX];
};

struct moa_binderlike_chan_info {
	int                                       id;
	struct moa_binderlike_arg_table           sq_info;
	struct moa_binderlike_arg_table           cq_info;
	unsigned int                              cache_cnt;
	unsigned int                              mmap_sz;
	unsigned int                              cq_offset;
	unsigned int                              usr_cnt;
};

/* this struct should export to userspace */
struct moa_binderlike_queue {
	volatile int head;
	volatile int tail;
	struct moa_binderlike_msg msgs[];
};


#define MOA_BINDERIOC_CREATE_CHAN _IOWR('B', 0, struct moa_binderlike_chan_info)

#endif

