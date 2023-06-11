#ifndef __MOA_V4L2STD_QUEUE_H__
#define __MOA_V4L2STD_QUEUE_H__
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <linux/mutex.h>

typedef int (*moa_v4l2std_fmt_update)(struct device *dev,
				      struct v4l2_format *fmt);

typedef int (*write_plane_addr)(u32 vout, u32 val);

struct moa_v4l2std_queue {
	struct vb2_queue q;
	struct list_head done_list;
	struct list_head inqueue_list;

	struct v4l2_format cur_fmt;
	moa_v4l2std_fmt_update update_cb;

	struct list_head ctx_node;

	u32 vout[3];

	struct mutex q_mutex;
};

struct moa_v4l2std_buf {
	struct vb2_v4l2_buffer vvb;
	struct list_head node;
};

int moa_v4l2std_queue_init(struct moa_v4l2std_queue *q, struct device *dev,
			   moa_v4l2std_fmt_update cb);

int moa_v4l2std_queue_handle_buf(struct moa_v4l2std_queue *q,
				 write_plane_addr cb);
int moa_v4l2std_queue_notify_complete(struct moa_v4l2std_queue *q);
#endif
