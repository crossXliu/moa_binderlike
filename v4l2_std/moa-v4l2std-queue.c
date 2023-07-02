#include <linux/module.h>
#include "moa-v4l2std-queue.h"
#include <media/videobuf2-dma-contig.h>

static int dbg_level;
module_param(dbg_level, int, 0644);

#define vb_to_mbuf(vb)                                                         \
	container_of_safe(container_of(vb, struct vb2_v4l2_buffer, vb2_buf),   \
			  struct moa_v4l2std_buf, vvb)

#define vb_to_mqueue(vb)                                                       \
	container_of_safe((vb)->vb2_queue, struct moa_v4l2std_queue, q)

#define log_dbg(fmt, arg...)                                                   \
	do {                                                                   \
		if (dbg_level > 1)                                             \
			pr_err("[%s](%d)" fmt, __func__, __LINE__, ##arg);     \
	} while (0)

#define log_info(fmt, arg...)                                                  \
	do {                                                                   \
		if (dbg_level > 0)                                             \
			pr_err("[%s](%d)" fmt, __func__, __LINE__, ##arg);     \
	} while (0)

#define log_err(fmt, arg...)                                                   \
	do {                                                                   \
		pr_err("[%s](%d)" fmt, __func__, __LINE__, ##arg);             \
	} while (0)

int moa_v4l2std_queue_handle_buf(struct moa_v4l2std_queue *q,
				 write_plane_addr cb)
{
	struct moa_v4l2std_buf *buf;
	struct vb2_buffer *vb;
	unsigned int i;
	buf = list_first_entry(&q->inqueue_list, typeof(*buf), node);

	vb = &buf->vvb.vb2_buf;

	if (cb) {
		if (vb->num_planes == 1) {
			/* handle with contingous buffer */
			u32 vaddr_base = (u32)vb2_plane_vaddr(vb, 0);
			for (i = 0; i < buf->comp_planes; i++) {
				u32 vaddr = vaddr_base + buf->comp_offsets[i];
				cb(q->vout[i], vaddr);
			}
		} else if (vb->num_planes > 1) {
			/* handle with multi plane in multi buffer */
			for (i = 0; i < vb->num_planes; i++)
				cb(q->vout[i], (u32)vb2_plane_vaddr(vb, i));
		}
	}

	list_move(&buf->node, &q->done_list);
	return 0;
}

int moa_v4l2std_queue_notify_complete(struct moa_v4l2std_queue *q)
{
	struct moa_v4l2std_buf *buf;
	struct vb2_buffer *vb;

	if (!q) {
		log_err("v4l2std queue is NULL\n");
		return -EINVAL;
	}

	if (list_empty(&q->done_list)) {
		log_err("no valid buffer in done queue\n");
		return -EINVAL;
	}

	buf = list_first_entry(&q->done_list, typeof(*buf), node);

	vb = &buf->vvb.vb2_buf;

	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	return 0;
}

static void moa_v4l2std_buf_queue(struct vb2_buffer *vb)
{
	struct moa_v4l2std_buf *buf;
	struct moa_v4l2std_queue *q;

	if (!vb) {
		log_err("invalid vb\n");
		return;
	}

	buf = vb_to_mbuf(vb);
	q = vb_to_mqueue(vb);

	list_add_tail(&buf->node, &q->inqueue_list);
	return;
}

static int moa_v4l2std_queue_setup(struct vb2_queue *q,
		   unsigned int *num_buffers, unsigned int *num_planes,
		   unsigned int sizes[], struct device *alloc_devs[])
{
	struct moa_v4l2std_queue *queue =
		container_of_safe(q, typeof(*queue), q);
	struct v4l2_pix_format_mplane *mp = &queue->cur_mfmt.vfmt.fmt.pix_mp;

	if (!queue) {
		log_err("vb2 queue from v4l2 framework is NULL\n");
		return -EINVAL;
	}

	/* TODO: adjust buffers according to capability */
	if (*num_buffers < q->min_buffers_needed)
		*num_buffers = q->min_buffers_needed;

	if (!queue->update_cb) {
		log_err("v4l2std queue have no callback\n");
		return -ENODEV;
	}

	queue->update_cb(q->dev, &queue->cur_mfmt);
	*num_planes = mp->num_planes;

	return 0;
}

static int moa_v4l2std_prepare_buf(struct vb2_buffer *vb)
{
	struct moa_v4l2std_queue *queue = vb_to_mqueue(vb);
	struct moa_v4l2std_buf *mbuf = vb_to_mbuf(vb);

	if (!queue || !mbuf) {
		log_err("vb2 queue from v4l2 framework is NULL\n");
		return -EINVAL;
	} else {
		struct v4l2_pix_format_mplane *pix_mp =
			&queue->cur_mfmt.vfmt.fmt.pix_mp;
		struct moa_v4l2std_fmt *mfmt = &queue->cur_mfmt;
		unsigned int i;

		mbuf->comp_planes = mfmt->vinfo.comp_planes;
		mbuf->comp_offsets[0] = 0;
		for (i = 1; i < ARRAY_SIZE(mbuf->comp_offsets); i++) {
			if (i < mbuf->comp_planes)
				mbuf->comp_offsets[i] =
					pix_mp->plane_fmt[0].bytesperline *
					pix_mp->height;
			else
				mbuf->comp_offsets[i] = 0;
		}
	}
	return 0;
}

static int moa_v4l2std_queue_streamon(struct vb2_queue *q, unsigned int count)
{
	/* TODO: connect to binderlike here */
	log_info(" entering stream on for v4l2 std\n");
	return 0;
}

static void moa_v4l2std_queue_streamoff(struct vb2_queue *q)
{
	/* TODO: connect to binderlike here */
	log_info(" entering stream off for v4l2 std\n");
}

static struct vb2_ops qops = {
	.queue_setup = moa_v4l2std_queue_setup,
	.buf_queue = moa_v4l2std_buf_queue,
	.buf_prepare = moa_v4l2std_prepare_buf,

	.start_streaming = moa_v4l2std_queue_streamon,
	.stop_streaming = moa_v4l2std_queue_streamoff,
};

static struct vb2_mem_ops moa_v4l2std_memops = {};

void *moa_v4l2std_buf_vaddr(void *buf_priv)
{
	struct vb2_mem_ops *ops = &moa_v4l2std_memops;
	u32 offset = 0;

	if (!ops->cookie)
		return NULL;

	return (void *)((u32)ops->cookie(buf_priv) + offset);
}

static struct vb2_mem_ops *moa_v4l2std_get_memops(void)
{
	moa_v4l2std_memops = vb2_dma_contig_memops;
	moa_v4l2std_memops.vaddr = moa_v4l2std_buf_vaddr;
	return &moa_v4l2std_memops;
}

int moa_v4l2std_queue_init(struct moa_v4l2std_queue *q, struct device *dev,
			   moa_v4l2std_fmt_update cb)
{
	struct vb2_queue *queue;
	if (!q)
		return -EINVAL;

	queue = &q->q;

	queue->ops = &qops;
	queue->mem_ops = moa_v4l2std_get_memops();

	mutex_init(&q->q_mutex);
	queue->lock = &q->q_mutex;

	q->update_cb = cb;
	return 0;
}
