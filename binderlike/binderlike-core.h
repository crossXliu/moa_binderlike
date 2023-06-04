#ifndef __BINDERLIKE_CORE_H__
#define __BINDERLIKE_CORE_H__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/cdev.h>

#include <linux/io.h>

struct moa_binderlike_queue_cap {
	int sq_offset;
	int cq_offset;
	int memblk_size;
};

#define MOA_BINDERIOC_GET_QUEUE _IOR('B', 0, struct moa_binderlike_queue_cap)
#endif

