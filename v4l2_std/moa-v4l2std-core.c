
// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-core.c - A moa V4l2 device Test Driver, core initialization
 *
 * Copyright 2023 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include "moa-cfgdev-core.h"
#include "moa-v4l2std-core.h"

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

static inline struct moa_v4l2std_device *file_to_mdev(struct file* f)
{
	struct video_device *port_dev = video_devdata(f);
	struct moa_v4l2std_device *mdev =
		container_of(port_dev, struct moa_v4l2std_device, port_dev);
	return mdev;
}

static int dbg_level = 2;
module_param(dbg_level, int, 0644);

static struct moa_v4l2std_fmt fmt_array[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp = 12,
		.buffers = 1,
		.planes = 2,
		.depth = { 8, 0 },
	},
};

struct moa_v4l2std_fmt *moa_v4l2std_match_fmt(int fourcc)
{
	int i;
	struct moa_v4l2std_fmt *fmt = fmt_array;
	for (i = 0; i < ARRAY_SIZE(fmt_array); i++) {
		if (fmt_array[i].fourcc == fourcc)
			break;
		fmt++;
	}

	if (i == ARRAY_SIZE(fmt_array))
		return NULL;
	return fmt;
}

static struct moa_v4l2std_device *g_mdev = NULL;

static const struct v4l2_file_operations moa_v4l2_fops = {
	.open = v4l2_fh_open,
	.release = v4l2_fh_release,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.read = NULL,
	.write = NULL,
};

static int moa_v4l2std_querycap(struct file *file, void *fh,
		       struct v4l2_capability *cap)
{
	int ret = 0;
	struct moa_v4l2std_device *mdev = file_to_mdev(file);
	struct video_device *port_dev = &mdev->port_dev;

	cap->capabilities = port_dev->device_caps;

	strscpy(cap->driver, "moa_v4l2std", sizeof(cap->driver));
	/* TODO: report the port number to user space */
	strscpy(cap->card, "port0", sizeof(cap->card));
	strscpy(cap->bus_info, "moa_isp", sizeof(cap->bus_info));
	return ret;
}

static int moa_v4l2std_enum_fmt(struct file *file, void *fh,
			       struct v4l2_fmtdesc *f)
{
	const struct moa_v4l2std_fmt *fmt;
	if (f->index >= ARRAY_SIZE(fmt_array))
		return -EINVAL;

	fmt = &fmt_array[f->index];

	f->pixelformat = fmt->fourcc;

	/* TODO: query the connectd sub-device */

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return 0;

	return 0;
}

static int moa_v4l2std_try_fmt(struct file *file, void *fh,
					     struct v4l2_format *f)
{
	const struct moa_v4l2std_fmt *fmt;
	struct v4l2_pix_format_mplane *mp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *pfmt = mp->plane_fmt;
	int p;
	
	fmt = moa_v4l2std_match_fmt(mp->pixelformat);

	if (!fmt) {
		log_err("no format 0x%08x valid\n", mp->pixelformat);
		mp->pixelformat = V4L2_PIX_FMT_NV12;
		fmt = moa_v4l2std_match_fmt(mp->pixelformat);
	}

	mp->field = V4L2_FIELD_NONE;

	/* TODO: fix the size of image */
	mp->width = 1920;
	mp->height = 1080;

	mp->num_planes = fmt->buffers;
	for (p = 0; p < mp->num_planes; p++) {
		u32 bpl = ALIGN(mp->width, 32);
		u32 stride = DIV_ROUND_UP(bpl * fmt->bpp, 8);
		pfmt[p].bytesperline = stride;
		pfmt[p].sizeimage = stride * mp->height;

		memset(pfmt[p].reserved, 0, sizeof(pfmt[p].reserved));
	}

	mp->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mp->quantization = V4L2_QUANTIZATION_DEFAULT;

	mp->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;

	memset(mp->reserved, 0, sizeof(mp->reserved));
	return 0;
}

static int moa_v4l2std_set_fmt_mp(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *mp = &f->fmt.pix_mp;
	struct video_device *dev = video_devdata(file);
	struct vb2_queue *q = dev->queue;
	struct moa_v4l2std_device *mdev =
		container_of(dev, typeof(*mdev), port_dev);

	int ret = moa_v4l2std_try_fmt(file, priv, f);
	if (ret < 0)
		return ret;

	if (vb2_is_busy(q))
		return -EBUSY;

	mdev->cur_v4l2_fmt = *f;
	mdev->cur_fmt = moa_v4l2std_match_fmt(mp->pixelformat);
	return 0;
}

static int moa_v4l2std_g_fmt_mp(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *mp = &f->fmt.pix_mp;
	struct video_device *dev = video_devdata(file);
	struct moa_v4l2std_device *mdev =
		container_of(dev, typeof(*mdev), port_dev);

	struct v4l2_pix_format_mplane *cur_mp = &mdev->cur_v4l2_fmt.fmt.pix_mp;

	memcpy(mp, cur_mp, sizeof(struct v4l2_pix_format_mplane));
	return 0;
}

static void moa_v4l2std_init_fmt(struct moa_v4l2std_device *mdev)
{
	const struct moa_v4l2std_fmt *fmt;
	struct v4l2_format *f = &mdev->cur_v4l2_fmt;
	struct v4l2_pix_format_mplane *mp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *pfmt = mp->plane_fmt;
	int p;

	memset(mp, 0, sizeof(*mp));
	fmt = &fmt_array[0];

	mp->field = V4L2_FIELD_NONE;

	mp->width = 1920;
	mp->height = 1080;

	mp->num_planes = fmt->buffers;
	for (p = 0; p < mp->num_planes; p++) {
		u32 bpl = ALIGN(mp->width, 32);
		u32 stride = DIV_ROUND_UP(bpl * fmt->bpp, 8);
		pfmt[p].bytesperline = stride;
		pfmt[p].sizeimage = stride * mp->height;

		memset(pfmt[p].reserved, 0, sizeof(pfmt[p].reserved));
	}

	mp->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mp->quantization = V4L2_QUANTIZATION_DEFAULT;

	mp->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;

	memset(mp->reserved, 0, sizeof(mp->reserved));
}

static const struct v4l2_ioctl_ops moa_v4l2_ioctl_ops = {
	.vidioc_querycap = moa_v4l2std_querycap,
	.vidioc_enum_fmt_vid_cap = moa_v4l2std_enum_fmt,

	.vidioc_try_fmt_vid_cap = NULL,
	.vidioc_s_fmt_vid_cap = NULL,
	.vidioc_g_fmt_vid_cap = NULL,
	.vidioc_try_fmt_vid_cap_mplane = moa_v4l2std_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = moa_v4l2std_set_fmt_mp,
	.vidioc_g_fmt_vid_cap_mplane = moa_v4l2std_g_fmt_mp,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,

	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static int moa_v4l2std_update_fmt(struct device *dev,
				  struct moa_v4l2std_fmt *mfmt)
{
	struct v4l2_device *vdev = dev_get_drvdata(dev);
	struct moa_v4l2std_device *mdev =
		container_of(vdev, typeof(*mdev), vdev);
	struct v4l2_pix_format_mplane *mp = &mfmt->vfmt.fmt.pix_mp;
	struct v4l2_pix_format_mplane *cur_mp = &mdev->cur_v4l2_fmt.fmt.pix_mp;

	memcpy(mp, cur_mp, sizeof(struct v4l2_pix_format_mplane));

	return 0;
}

static void moa_v4l2std_video_dev_release(struct video_device *vdev)
{
	/* TODO: a void release, implement it later */
}

static int moa_v4l2std_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct moa_v4l2std_device *mdev;

	log_info("++.\n");
	// make sure that moa_v4l2std_device is an singltance
	if (g_mdev) {
		log_info("exiting moa v4l2std device\n");
		ret = -EINVAL;
		goto end;
	} else {
		mdev = kzalloc(sizeof(*g_mdev), GFP_KERNEL);
	}

	if (!mdev) {
		ret = -ENOMEM;
		goto end;
	} else {
		g_mdev = mdev;
	}

	// set v4l2 device info and register
	snprintf(mdev->vdev.name, sizeof(mdev->vdev.name), "moa-core");
	ret = v4l2_device_register(&pdev->dev, &mdev->vdev);
	if (ret < 0)
		goto clean_up;

	mdev->vdev.ctrl_handler = NULL; // FIXME

	moa_v4l2std_queue_init(&mdev->queue, &pdev->dev,
			       moa_v4l2std_update_fmt);

	moa_v4l2std_init_fmt(mdev);

	// set video device info and register
	{
		struct video_device *port_dev = &mdev->port_dev;
		snprintf(port_dev->name, sizeof(port_dev->name), "moa-core");
		port_dev->v4l2_dev = &mdev->vdev;
		port_dev->fops = &moa_v4l2_fops;

		port_dev->ioctl_ops = &moa_v4l2_ioctl_ops;
		port_dev->release = moa_v4l2std_video_dev_release;

		port_dev->device_caps = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_IO_MC |
					V4L2_CAP_VIDEO_CAPTURE_MPLANE |
					V4L2_CAP_STREAMING;

		port_dev->queue = &mdev->queue.q;

		mutex_init(&mdev->port_lock);
		port_dev->lock = &mdev->port_lock;

		ret = video_register_device(port_dev, VFL_TYPE_VIDEO, -1);
		video_set_drvdata(port_dev, mdev);
	}


	log_info("--.\n");
	return ret;
clean_up:
	kfree(g_mdev);
	g_mdev = NULL;
end:
	return ret;
}

void moa_v4l2std_dev_release(struct device *dev)
{
	struct v4l2_device *vdev = dev_get_drvdata(dev);
	struct moa_v4l2std_device *mdev = NULL;

	if (!vdev) {
		log_err(" dev drvdata has been corrupted\n");
		return;
	}

	mdev = container_of(vdev, struct moa_v4l2std_device, vdev);
	if (mdev != g_mdev) {
		log_err(" dev drvdata has been corrupted\n");
	}

	kfree(mdev);
	g_mdev = NULL;
}

int moa_v4l2std_driver_remove(struct platform_device *pdev)
{
	int ret = 0;	
	// TODO
	return ret;
}

static struct of_device_id v4l2std_match[] = {
	{
		.compatible = "moa,v4l2std",
		.data = NULL,
	},
	{},
};

static struct platform_driver moa_v4l2std_driver = {
	.probe = moa_v4l2std_probe,
	.remove = moa_v4l2std_driver_remove,
	.driver = {
		.name = "moa,v4l2std",
		.of_match_table = of_match_ptr(v4l2std_match),
	}
};

int __init moa_driver_init(void)
{
	int ret = 0;
	log_info("++.\n");
	
	ret = platform_driver_register(&moa_v4l2std_driver);
	
	if (ret < 0) {
		log_info("moa v4l2std driver register fail\n");
	}

	log_info("--.\n");
	return ret;
}

void __exit moa_driver_exit(void)
{
	platform_driver_register(&moa_v4l2std_driver);
}

module_init(moa_driver_init);
module_exit(moa_driver_exit);
