#include "binderlike-core.h"
#include <linux/fs.h>
#include <linux/dma-mapping.h>

struct moa_binderlike_msg {
	char content[256];
};

/* this struct should export to userspace */
struct moa_binderlike_queue {
	volatile int head;
	volatile int tail;
	int total_len;
	struct moa_binderlike_msg msgs[];
};

struct moa_binderlike_device {
	u32 id;

	struct platform_device *pdev;

	u32 dma_addr;

	int queue_len;
	struct moa_binderlike_queue *sq;
	struct moa_binderlike_queue *cq;
	int sq_size;
	int cq_size;
	int memblk_size;

	struct cdev cdev;
};

static struct moa_binderlike_device *g_bdev = NULL;
static struct class *moa_binderlike_class = NULL;

static int dbg_level = 2;
module_param(dbg_level, int, 0644);

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

static void moa_binderlike_parse_dt(struct moa_binderlike_device *bdev)
{
	struct device_node *np = bdev->pdev->dev.of_node;
	if (of_property_read_u32(np, "queue_len", &bdev->queue_len) < 0) {
		log_info("no valid queue_len in dts, set to 32 by default\n");
		bdev->queue_len = 32;
	}
}

static DEFINE_SPINLOCK(wr_spin);

static int moa_binderlike_queue_addmsg(const char *buf, size_t len)
{
	struct moa_binderlike_device *bdev = g_bdev;
	struct moa_binderlike_queue *q;
	struct moa_binderlike_msg *msg;
	u32 cur, new_tail;
	size_t sz;

	if (!bdev) {
		log_err("binderlike device is not valid\n");
		return -ENOTTY;
	}

	if (!buf || len + 1 >= sizeof(bdev->sq->msgs[0].content)) {
		log_err("buf %px, len %d wrong", buf, len);
		return -ENOSPC;
	}

	q = bdev->sq;

	spin_lock(&wr_spin);

	cur = q->tail;
	new_tail = (q->tail + 1) % q->total_len;

	if (q->head % q->total_len == new_tail) {
		log_err("submit queue is full\n");
		spin_unlock(&wr_spin);
		return -EBUSY;
	}

	q->tail = new_tail;
	spin_unlock(&wr_spin);

	msg = &q->msgs[cur];
	sz = snprintf(msg->content, sizeof(msg->content), "%s", buf);

	if (msg->content[sz - 1] == '\n')
		msg->content[sz - 1] = '\0';

	log_dbg("add msg to slot %d, size %d, current head %d [%s]\n", cur, sz,
		q->head, msg->content);
	return sz;
}

int moa_binderlike_queue_getmsg(char *buf, size_t len)
{
	struct moa_binderlike_device *bdev = g_bdev;
	struct moa_binderlike_queue *q;
	u32 cur;
	size_t sz;

	if (!bdev) {
		log_err("binderlike device is not valid\n");
		return -ENOTTY;
	}

	if (!buf || len < strlen(bdev->sq->msgs[0].content) + 1) {
		log_err("buf %px, len %d wrong", buf, len);
		return -ENOSPC;
	}

	q = bdev->sq;
	if (q->head == q->tail) {
		log_err("submit queue is empty\n");
		return -ENOMEM;
	}

	sz = snprintf(buf, len, "%s", q->msgs[q->head].content);

	if (sz > 0 && buf[sz - 1] == '\n')
		buf[sz - 1] = '\0';


	cur = (q->head + 1) % q->total_len;
	q->head = cur;

	log_dbg("head %d - 1 have been read, [%s]\n", q->head, buf);

	return sz;
}

int moa_binderlike_open(struct inode *inode, struct file *filp)
{
	return 0;
}

int moa_binderlike_frelease(struct inode *inode, struct file *filp)
{
	return 0;
}

ssize_t moa_binderlike_read(struct file *filp, char __user *buf, size_t len,
			    loff_t *offset)
{
	size_t sz;
	char sbuf[256];
	sz = moa_binderlike_queue_getmsg(sbuf, sizeof(sbuf));

	if (sz <= 0)
		return sz;
	return copy_to_user(buf, sbuf, sizeof(sbuf));
}

ssize_t moa_binderlike_write(struct file *filp, const char __user *buf,
			     size_t len, loff_t *offset)
{
	int ret = 0;
	char sbuf[256];

	if (len + 1 >= sizeof(sbuf)) {
		log_err("msg size %d is too long", len);
		return -EFAULT;
	}

	if (copy_from_user(sbuf, buf, sizeof(sbuf))) {
		log_err("copy buffer from userspace failed\n");
		return ret;
	}

	return moa_binderlike_queue_addmsg(sbuf, len);
}

static int moa_binderlike_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t mmap_area_sz = PAGE_ALIGN(g_bdev->memblk_size);
	pgprot_t prot = pgprot_noncached(vma->vm_page_prot);

	if (vma->vm_end - vma->vm_start > mmap_area_sz) {
		log_err("mmap size %lu is too large to map\n",
			vma->vm_end - vma->vm_start);
		return -EINVAL;
	}

	if (remap_pfn_range(vma, vma->vm_start,
			    g_bdev->dma_addr >> PAGE_SHIFT,
			    vma->vm_end - vma->vm_start, prot) < 0)
		return -EAGAIN;

	return 0;
}

static long moa_binderlike_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long args)
{
	void __user *argp = (void __user *)args;
	switch (cmd) {
	case MOA_BINDERIOC_GET_QUEUE: {
		struct moa_binderlike_queue_cap cap;
		if (copy_from_user(&cap, argp, sizeof(cap)) < 0) {
			log_err("copy from user failed\n");
			return -EFAULT;
		}

		cap.sq_offset = 0;
		cap.cq_offset = g_bdev->sq_size;
		cap.memblk_size = g_bdev->memblk_size;

		if (copy_to_user(argp, &cap, sizeof(cap)) < 0) {
			log_err("copy to user failed\n");
			return -EFAULT;
		}

	} break;
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

static int moa_binderlike_alloc_queue(struct moa_binderlike_device *bdev)
{
	struct moa_binderlike_queue *sq, *cq;
	size_t queue_size = sizeof(struct moa_binderlike_queue) +
			    bdev->queue_len * sizeof(struct moa_binderlike_msg);

	void *memblk = dma_alloc_coherent(&bdev->pdev->dev, queue_size * 2,
					  &bdev->dma_addr, GFP_KERNEL);

	log_info("binderlike create queue, memblk size %u\n", queue_size);

	if (!memblk) {
		log_err("alloc mem for sq failed\n");
		return -ENOMEM;
	}

	bdev->memblk_size = queue_size * 2;

	sq = (struct moa_binderlike_queue *)memblk;
	if (!sq) {
		log_err("alloc mem for sq failed\n");
		return -ENOMEM;
	}

	cq = (struct moa_binderlike_queue *)(memblk + queue_size);
	if (!cq) {
		log_err("alloc mem for cq failed\n");
		return -ENOMEM;
	}

	sq->total_len = bdev->queue_len;
	cq->total_len = bdev->queue_len;

	bdev->sq = sq;
	bdev->sq_size = queue_size;

	bdev->cq = cq;
	bdev->cq_size = queue_size;
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

	if (moa_binderlike_alloc_queue(bdev) < 0)
		goto clean_up;

	if (moa_binderlike_create_node(bdev) < 0)
		goto clean_up;

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
