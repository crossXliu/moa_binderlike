#ifndef __BINDERLIKE_CORE_H__
#define __BINDERLIKE_CORE_H__

#include <linux/io.h>

#define BINDERLIKE_INPUT_PARAM_MAX 6
#define BINDERLIKE_CHAN_MAX 16

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
	struct moa_binderlike_arg_table           sq_info;
	struct moa_binderlike_arg_table           cq_info;
	unsigned int                              cache_cnt;
	unsigned int                              mmap_sz;
};

#define MOA_BINDERIOC_GET_QUEUE _IOR('B', 0, struct moa_binderlike_queue_cap)
#define MOA_BINDERIOC_CREATE_CHAN _IOW('B', 0, struct moa_binderlike_queue_cap)

#endif

