#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>

#include "binderlike-core.h"

enum queue_status {
	UNINIT,
	INITED,
};

struct moa_binderlike_chan_queue {
	struct moa_binderlike_arg_table arg_table;
	int q_size;
	unsigned int cache_cnt;
	enum queue_status status;
	dma_addr_t dma_addr;
	struct moa_binderlike_queue *q;
};

struct moa_binderlike_chan {
	int                                       chan_id;
	struct moa_binderlike_chan_queue          sq;
	struct moa_binderlike_chan_queue          cq;
	unsigned int                              memblk_size;
	dma_addr_t                                dma_addr;
	struct list_head                          chan_node;
};

struct moa_binderlike_device {
	struct platform_device                   *pdev;
	int                                       max_queue_len;
	struct cdev                               cdev;

	struct list_head                          chan_head;
	struct moa_binderlike_chan               *chan_map[BINDERLIKE_CHAN_MAX];
};

struct moa_binderlike_fh {
	struct moa_binderlike_chan                *chan;
};

static struct moa_binderlike_device *g_bdev = NULL;
static struct class *moa_binderlike_class = NULL;

static int dbg_level = 2;
module_param(dbg_level, int, 0644);

static int default_chan = 1;
module_param(default_chan, int, 0644);

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

#define FILE_TO_CHAN(filp)                                                     \
	(((struct moa_binderlike_fh *)(filp->private_data))->chan)

static void moa_binderlike_parse_dt(struct moa_binderlike_device *bdev)
{
	int ret;
	struct device_node *np = bdev->pdev->dev.of_node;

	ret = of_property_read_u32(np, "max_queue_len", &bdev->max_queue_len);
	if (ret < 0) {
		log_info("no valid max_queue_len in dts, set 32 by default\n");
		bdev->max_queue_len = 32;
	}
}

static DEFINE_SPINLOCK(wr_spin);

static int moa_binderlike_queue_addmsg(struct moa_binderlike_chan *chan,
				       const char *buf, size_t len)
{
	struct moa_binderlike_chan_queue *sq;
	struct moa_binderlike_queue *queue;
	struct moa_binderlike_msg *msg;
	u32 cur, new_tail;
	size_t sz;

	if (!chan) {
		log_err("binderlike device is not valid\n");
		return -ENOTTY;
	}

	if (!buf || len + 1 >= sizeof(sq->q->msgs[0].content)) {
		log_err("buf %px, len %d wrong", buf, len);
		return -ENOSPC;
	}

	sq = &chan->sq;
	queue = sq->q;

	spin_lock(&wr_spin);

	cur = queue->tail;
	new_tail = (queue->tail + 1) % sq->cache_cnt;

	if (queue->head % sq->cache_cnt == new_tail) {
		log_err("submit queue is full\n");
		spin_unlock(&wr_spin);
		return -EBUSY;
	}

	queue->tail = new_tail;
	spin_unlock(&wr_spin);

	msg = &queue->msgs[cur];
	sz = snprintf(msg->content, sizeof(msg->content), "%s", buf);

	if (msg->content[sz - 1] == '\n')
		msg->content[sz - 1] = '\0';

	log_dbg("add msg to slot %d, size %d, current head %d [%s]\n", cur, sz,
		queue->head, msg->content);
	return sz;
}

int moa_binderlike_queue_getmsg(struct moa_binderlike_chan *chan, char *buf,
				size_t len)
{
	struct moa_binderlike_queue *q;
	u32 cur;
	size_t sz;

	if (!chan) {
		log_err("this chan is not valid\n");
		return -ENOTTY;
	}

	if (!buf || len < sizeof(q->msgs[0].content)) {
		log_err("buf %px, len %d wrong", buf, len);
		return -ENOSPC;
	}

	q = chan->sq.q;
	if (q->head == q->tail) {
		log_err("submit queue is empty\n");
		return -ENOMEM;
	}

	sz = snprintf(buf, len, "%s", q->msgs[q->head].content);

	if (sz > 0 && buf[sz - 1] == '\n')
		buf[sz - 1] = '\0';


	cur = (q->head + 1) % chan->sq.cache_cnt;
	q->head = cur;

	log_dbg("head %d - 1 have been read, [%s]\n", q->head, buf);

	return sz;
}

static void bind_chan_and_fh(struct moa_binderlike_chan *chan, struct moa_binderlike_fh *fh)
{
	fh->chan = chan;
	chan->usr_cnt++;
	return;
}

static void unbind_chan_and_fh(struct moa_binderlike_chan *chan, struct moa_binderlike_fh *fh)
{
	if (!fh)
		return;
	fh->chan->usr_cnt--;
	if (fh->chan->usr_cnt == 0) {
		// TODO: release action
	}
	fh->chan = NULL;
	return;
}

int moa_binderlike_open(struct inode *inode, struct file *filp)
{
	struct moa_binderlike_fh *fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	filp->private_data = fh;
	return 0;
}

int moa_binderlike_frelease(struct inode *inode, struct file *filp)
{
	struct moa_binderlike_fh *fh = filp->private_data;
	struct moa_binderlike_chan *chan;

	if (!fh)
		return -ENODEV;
	chan = fh->chan;
	if (!chan)
		goto fh_out;

	/* the sq's q addr is eq to cpu addr */
	dma_free_coherent(&g_bdev->pdev->dev, chan->memblk_size, chan->sq.q,
			  chan->dma_addr);
	kfree(fh->chan);
fh_out:
	kfree(fh);
	return 0;
}

ssize_t moa_binderlike_read(struct file *filp, char __user *buf, size_t len,
			    loff_t *offset)
{
	size_t sz;
	char sbuf[256];
	struct moa_binderlike_chan *chan;

	chan = g_bdev->chan_map[0];
	if (!chan) {
		log_err("chan is not inited\n");
		return -ENODEV;
	}

	sz = moa_binderlike_queue_getmsg(chan, sbuf, sizeof(sbuf));

	if (sz <= 0)
		return sz;
	return copy_to_user(buf, sbuf, sizeof(sbuf));
}

ssize_t moa_binderlike_write(struct file *filp, const char __user *buf,
			     size_t len, loff_t *offset)
{
	int ret = 0;
	char sbuf[256];
	struct moa_binderlike_chan *chan;

	if (len + 1 >= sizeof(sbuf)) {
		log_err("msg size %d is too long", len);
		return -EFAULT;
	}

	/* in debug mode, we just use chan 0 */
	chan = g_bdev->chan_map[0];
	if (!chan) {
		log_err("chan is not init\n");
		return -ENODEV;
	}

	if (copy_from_user(sbuf, buf, sizeof(sbuf))) {
		log_err("copy buffer from userspace failed\n");
		return ret;
	}

	sbuf[len] = '\0';

	return moa_binderlike_queue_addmsg(chan, sbuf, len);
}

static int moa_binderlike_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct moa_binderlike_fh *fh =
		(struct moa_binderlike_fh *)filp->private_data;
	struct moa_binderlike_chan *chan = fh->chan;

	size_t mmap_area_sz = PAGE_ALIGN(chan->memblk_size);
	pgprot_t prot = pgprot_noncached(vma->vm_page_prot);

	if (vma->vm_end - vma->vm_start > mmap_area_sz) {
		log_err("mmap size %lu is too large to map\n",
			vma->vm_end - vma->vm_start);
		return -EINVAL;
	}

	if (remap_pfn_range(vma, vma->vm_start, chan->dma_addr >> PAGE_SHIFT,
			    vma->vm_end - vma->vm_start, prot) < 0)
		return -EAGAIN;

	return 0;
}

static inline unsigned int
cal_binderlike_entry_size(const struct moa_binderlike_arg_table *table)
{
	unsigned int entry_len = 0, i;
	for (i = 0; i < table->argc && i < BINDERLIKE_INPUT_PARAM_MAX; i++) {
		entry_len += table->arg_size[i];
	}
	return entry_len;
}

static unsigned int
cal_binderlike_chan_size(const struct moa_binderlike_chan_info *info,
			 unsigned int *cq_offset)
{
	unsigned int sz_queue = 0, sz_total = 0;
	unsigned int sz_entry = cal_binderlike_entry_size(&info->sq_info);

	/* calculate sq size */
	sz_queue = sizeof(struct moa_binderlike_queue) +
		   sz_entry * info->cache_cnt;
	sz_queue = ALIGN(sz_queue, sizeof(dma_addr_t));
	sz_total += sz_queue;
	*cq_offset = sz_total;

	/* calculate cq size */
	sz_entry = cal_binderlike_entry_size(&info->cq_info);
	sz_queue = sizeof(struct moa_binderlike_queue) +
		   sz_entry * info->cache_cnt;
	sz_queue = ALIGN(sz_queue, sizeof(dma_addr_t));
	sz_total += sz_queue;

	return sz_total;
}

static int moa_binderlike_register_chan(struct moa_binderlike_device *bdev,
					struct moa_binderlike_chan *chan)
{
	int i;
	if (!bdev)
		return -ENODEV;

	if (chan->chan_id >= 0)
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(bdev->chan_map); i++) {
		if (bdev->chan_map[i] == NULL)
			break;
	}

	if (i == ARRAY_SIZE(bdev->chan_map)) {
		log_err("no available chan id\n");
		return -EBUSY;
	}

	log_info("register chan as id %u\n", i);
	chan->chan_id = i;
	bdev->chan_map[i] = chan;
	list_add_tail(&chan->chan_node, &bdev->chan_head);
	return 0;
}

static int moa_binderlike_acquire_chan(struct moa_binderlike_chan_info *info,
                                       int *chan_id)
{
	int expected_id = chan->id;
	struct moa_binderlike_chan *chan;
	if (expected_id >= BINDERLIKE_CHAN_MAX)
		return -EINVAL;

	if (expected_id < 0)
		return moa_binderlike_create_chan(info, chan_id);

	chan = gbdev->chan_map[expected_id];
	if (!chan) {
		log_err("the chan %d is not init\n", expected_id);
		return -ENODEV;
	}

	chan_id = expected_id;
	info->id = chan_id;
	info->cache_cnt = chan->sq.cache_cnt;
        info->mmap_sz =
            (unsigned int)(&chan->cq.q) - (unsigned int)(&chan->sq.q);

        return 0;
}

static int
moa_binderlike_create_chan(struct moa_binderlike_chan_info *info, int *chan_id)
{
	struct moa_binderlike_chan *chan;
	unsigned int cq_offset, sz_total;
	void *cpu_addr;
	int ret;

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	/* alloc memory from default dma */
	sz_total = cal_binderlike_chan_size(info, &cq_offset);
	sz_total = PAGE_ALIGN(sz_total);

	cpu_addr = dma_alloc_coherent(&g_bdev->pdev->dev, sz_total,
				      &chan->dma_addr, GFP_KERNEL);

	/* report mmap size for user app mmap */
	if (!cpu_addr) {
		log_err("no dma coontingous area %u for binderlike chan\n",
			sz_total);
		ret = -ENOMEM;
		goto clean_up;
	}
	chan->memblk_size = sz_total;

	chan->sq.q = (struct moa_binderlike_queue *)cpu_addr;
	chan->cq.q = (struct moa_binderlike_queue *)cpu_addr +
		   cq_offset;
	chan->sq.cache_cnt = info->cache_cnt;
	chan->cq.cache_cnt = info->cache_cnt;

	chan->chan_id = -1;
	INIT_LIST_HEAD(&chan->chan_node);

	ret = moa_binderlike_register_chan(g_bdev, chan);
	if (ret < 0) {
		log_err("register chan to binderlike dev fail\n");
		goto clean_up;
	}

	info->mmap_sz = chan->memblk_size;
	info->cq_offset = cq_offset;
	*chan_id = chan->chan_id;
	return ret;

clean_up:
	kfree(chan);
	return ret;
}

static void moa_binderlike_adjust_info(struct moa_binderlike_chan_info *info)
{
	unsigned int max_len = g_bdev->max_queue_len;
	struct moa_binderlike_msg *msg;

	if (info->cache_cnt > max_len)
		info->cache_cnt = max_len;

	/* TODO: we just enforce arg as 1 string now for isp api chan */
	/* remove it later */
	info->sq_info.argc = 1;
	info->sq_info.arg_size[0] = sizeof(msg->content);
	info->cq_info.argc = 1;
	info->cq_info.arg_size[0] = sizeof(msg->content);
}

static long moa_binderlike_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long args)
{
	void __user *argp = (void __user *)args;
	struct moa_binderlike_fh *fh = filp->private_data;
	long ret;
	switch (cmd) {
	case MOA_BINDERIOC_CREATE_CHAN:
	{
		struct moa_binderlike_chan_info info;
		int new_id;
		ret = copy_from_user(&info, argp, sizeof(info));
		if (ret < 0) {
			log_err("copy from user failed\n");
			return ret;
		}

		moa_binderlike_adjust_info(&info);

		ret = moa_binderlike_create_chan(&info, &new_id);
		if (ret < 0) {
			log_err("binderlike chan create failed, ret %ld\n",
				ret);
			return ret;
		}
		fh->chan = g_bdev->chan_map[new_id];

		ret = copy_to_user(&argp, &info, sizeof(info));
		if (ret < 0) {
			log_err("copy to user failed\n");
			return ret;
		}
		break;
	}
	default:
		log_err("unknown cmd %u\n", cmd);
		break;
	}
	return 0;
}

static struct file_operations binderlike_fops = {
	.open = moa_binderlike_open,
	.release = moa_binderlike_frelease,

	.read = moa_binderlike_read,
	.write = moa_binderlike_write,

	.mmap = moa_binderlike_mmap,
	.unlocked_ioctl = moa_binderlike_ioctl,
};

static int moa_binderlike_create_node(struct moa_binderlike_device *bdev)
{
	dev_t dev = 0;

	cdev_init(&bdev->cdev, &binderlike_fops);

	if (alloc_chrdev_region(&dev, 0, 1, "moa_binderlike_c") < 0) {
		log_err("no dev_t of char dev domain vaild, alloc fail\n");
		return -EBUSY;
	}

	log_info("binderlike node [%d, %d] region allocated\n", MAJOR(dev),
		 MINOR(dev));

	if (cdev_add(&bdev->cdev, dev, 1) < 0) {
		log_err("register cdev fail\n");
		return -EBUSY;
	}

	device_create(moa_binderlike_class, &bdev->pdev->dev, dev, NULL,
		      "moa_binderlike");
	return 0;
}

static int moa_binderlike_create_default_chan(void)
{
	struct moa_binderlike_chan_info info = {
		.sq_info = { 1, { 0 }, },
		.cq_info = { 1, { 0 }, },
		.cache_cnt = 32,
		.mmap_sz = 0,
	};
	int chan_id, ret;

	ret = moa_binderlike_create_chan(&info, &chan_id);
	if (ret < 0)
		return ret;

	log_info("create default chan as id %d\n", chan_id);
	return 0;
}

static int moa_binderlike_probe(struct platform_device *pdev)
{
	struct moa_binderlike_device *bdev;
	int ret = 0;

	log_info("enter ++\n");

	bdev = kzalloc(sizeof(*bdev), GFP_KERNEL);
	if (!bdev) {
		log_err("alloc binderlike device failed, no memory\n");
		ret = -ENOMEM;
	}

	bdev->pdev = pdev;
	g_bdev = bdev;

	moa_binderlike_parse_dt(bdev);
	INIT_LIST_HEAD(&bdev->chan_head);

	if (moa_binderlike_create_node(bdev) < 0)
		goto clean_up;

	if (default_chan && moa_binderlike_create_default_chan() < 0)
		log_err("creating default chan fail\n");

	return 0;
	log_info("exit --\n");
clean_up:
	kfree(bdev);
	g_bdev = NULL;

	return 0;
}

static const struct of_device_id moa_binderlike_match[] = {
	{
		.compatible = "moa,binderlike",
		.data = NULL,
	},
	{},
};

struct platform_driver binderlike_driver = {
	.probe = moa_binderlike_probe,
	.driver = {
		.name = "moa,binderlike",
		.of_match_table = of_match_ptr(moa_binderlike_match),
	},
};

int binderlike_driver_init(void)
{
	int ret;
	moa_binderlike_class = class_create(THIS_MODULE, "moa_binderlike");
	ret = platform_driver_register(&binderlike_driver);
	if (ret < 0) {
		log_err("ret %d, register driver failed\n", ret);
	}
	return ret;
}

void binderlike_driver_exit(void)
{
	return platform_driver_unregister(&binderlike_driver);
}

module_init(binderlike_driver_init);
module_exit(binderlike_driver_exit);
