#ifndef __MOA_V4L2STD_CORE_H__
#define __MOA_V4L2STD_CORE_H__

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-core.h>
#include <linux/mutex.h>
#include "moa-v4l2std-queue.h"

#define V4L2STD_PLANE_MAX 8

struct moa_v4l2std_fmt {
	u32 fourcc;
	int bpp;
	int buffers;
	int planes;
	int depth[V4L2STD_PLANE_MAX];
};

struct moa_v4l2std_device {
	struct video_device port_dev;
	struct v4l2_device vdev;

	/* queue relevant data type */
	struct vb2_queue q;
	struct list_head done_buf_list;
	struct list_head inqueued_buf_list;

	struct moa_v4l2std_queue queue;

	struct moa_v4l2std_fmt *cur_fmt;
	struct v4l2_format cur_v4l2_fmt;

	struct mutex port_lock;
};

#endif // __MOA_V4L2STD_CORE_H__
