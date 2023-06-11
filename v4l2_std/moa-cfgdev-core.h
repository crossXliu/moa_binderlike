#ifndef __MOA_CFGDEV_CORE_H__
#define __MOA_CFGDEV_CORE_H__
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "moa-v4l2std-queue.h"


enum isp_irq {
	SOF = 0,
	SOL = 1,
	_3A = 2,
	VOUT_DONE = 3,
};

#define PLANE_MAX 16
typedef int (*irq_handle)(int irq, void *data);

int moa_cfg_register_irqhandle(int ctx, irq_handle handle);
int moa_cfg_unregister_irqhandle(int ctx);

int moa_cfgdev_bind_queue(unsigned int ctx, int vout,
			  struct moa_v4l2std_queue *q);
int moa_cfgdev_unbind_queue(unsigned int ctx, struct moa_v4l2std_queue *q);
#endif
