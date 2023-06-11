#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include "moa-cfgdev-core.h"

static int dbg_level = 2;
module_param(dbg_level, int, 0644);

static int dbg_fake_interrupt_run = 1;
module_param(dbg_fake_interrupt_run, int, 0644);

static int dbg_thd_running = 1;
module_param(dbg_thd_running, int, 0644);

struct moa_cfg_dev {
	int usr_cnt;
	char name[256];

	struct platform_device *pdev;

	u32 irq_setting;
	u32 irq_num;

	/* for simu */
	struct task_struct *fake_irq_thread;
};

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

static struct moa_cfg_dev *g_dev = NULL;

static irq_handle fake_irq_handle = NULL;

struct moa_v4l2std_bound {
	int ctx;
	int vout;
	struct moa_v4l2std_queue *q;
};

#define V4L2STD_CTX_MAX 8
static struct list_head ctx_arrqy[V4L2STD_CTX_MAX];

int moa_cfgdev_bind_queue(unsigned int ctx, int vout,
			  struct moa_v4l2std_queue *q)
{
	struct list_head *head;
	if (ctx >= V4L2STD_CTX_MAX) {
		log_err("ctx %u is too max", ctx);
		return -EINVAL;
	}

	/* TODO: register vout here */
	(void)vout;
	head = &ctx_arrqy[ctx];

	INIT_LIST_HEAD(&q->ctx_node);
	list_add_tail(&q->ctx_node, head);
	return 0;
}

int moa_cfgdev_unbind_queue(unsigned int ctx, struct moa_v4l2std_queue *q)
{
	list_del(&q->ctx_node);
	return 0;
}

int moa_cfgdev_parse_dt(struct moa_cfg_dev *cfg_dev)
{
	struct device_node *np = cfg_dev->pdev->dev.of_node;

	if (of_property_read_u32(np, "irq-setting", &cfg_dev->irq_setting) <
	    0) {
		log_err("fail to read irq-setting\n");
	}

	cfg_dev->irq_num = of_irq_get_byname(np, "ue");
	if (cfg_dev->irq_num < 0) {
		log_err("fail to read irq number\n");
	}
	return 0;
}

int moa_cfg_register_irqhandle(int ctx, irq_handle handle)
{
	(void)ctx;
	if (fake_irq_handle)
		return -EBUSY;

	fake_irq_handle = handle;
	return 0;
}

int moa_cfg_unregister_irqhandle(int ctx)
{
	(void)ctx;
	if (!fake_irq_handle)
		return -ENOTTY;

	fake_irq_handle = NULL;
	return 0;
}

static enum irqreturn irq_common(int irq, void *data)
{
	if (fake_irq_handle)
		fake_irq_handle(irq, data);
	return 0;
}

static int moa_cfg_dev_fake_irq(void *data)
{
	struct moa_cfg_dev *cdev = (struct moa_cfg_dev *)data;
	u32 loop = 0;

	while (1) {

		int irq_num = loop % 4;
		msleep(30);

		if (!dbg_thd_running) {
			loop++;
			continue;
		}

		(void)irq_common(irq_num, (void *)cdev);

		if (loop == SOL) {
			struct moa_v4l2std_queue *q;
			list_for_each_entry(q, &ctx_arrqy[0], ctx_node) {
				moa_v4l2std_queue_handle_buf(q, NULL);
			}
		}

		if (loop == VOUT_DONE) {
			struct moa_v4l2std_queue *q;
			list_for_each_entry(q, &ctx_arrqy[0], ctx_node) {
				moa_v4l2std_queue_notify_complete(q);
			}
		}

		loop++;
	}
	log_info(" loop %d, cdev %s thread end\n", loop, cdev->name);

	return 0;
}

static int moa_cfgdev_irq_init(struct moa_cfg_dev *cfg_dev)
{
	int ret = 0;
	u32 irq_num = cfg_dev->irq_num;
	struct device *dev = &cfg_dev->pdev->dev;
	if (!dbg_fake_interrupt_run) {
		/* TODO: set irq flag */
		ret = devm_request_irq(dev, irq_num, irq_common, 0,
				       "moa-cfg-irq comming", (void *)cfg_dev);
		if (ret < 0)
			log_err("req irq %d fail\n", irq_num);
	} else {
		cfg_dev->fake_irq_thread = kthread_run(
			moa_cfg_dev_fake_irq, (void *)cfg_dev, "moa_irq_fake");
		if (cfg_dev->fake_irq_thread == NULL) {
			log_err("no valid irq thread created\n");
			ret = -EBUSY;
		}

	}

	return ret;
}

static void moa_cfgdev_context_init(struct moa_cfg_dev *cdev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(ctx_arrqy); i++)
		INIT_LIST_HEAD(&ctx_arrqy[i]);
}

static int moa_cfgdev_probe(struct platform_device *pdev)
{
	int ret = 0;
	int off;
	struct moa_cfg_dev *cdev;

	g_dev = kzalloc(sizeof(*g_dev), GFP_KERNEL);
	if (!g_dev) {
		log_err("alloc memory for moa cfgdev failed\n");
		ret = -ENOMEM;
	}

	cdev = g_dev;
	cdev->pdev = pdev;

	off = snprintf(cdev->name, sizeof(cdev->name), "moa-cfg");
	cdev->name[off] = '\0';

	if (moa_cfgdev_parse_dt(cdev) < 0) {
		log_err("no valid dts property\n");
		ret = -ENOTTY;
		goto cleanup;
	}

	if (moa_cfgdev_irq_init(cdev) < 0)
		goto cleanup;

	moa_cfgdev_context_init(cdev);

	return ret;
cleanup:
	kfree(g_dev);
	g_dev = NULL;
	return ret;
}

static struct of_device_id moa_cfgdev_match[] = {
	{
		.compatible = "moa,cfgdev",
		.data = NULL,
	},
	{},
};

struct platform_driver moa_cfg_driver = {
	.probe = moa_cfgdev_probe,
	.driver = {
		.name = "moa,cfgdev",
		.of_match_table = of_match_ptr(moa_cfgdev_match),
	},
};

int moa_cfgdev_driver_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&moa_cfg_driver);
	if (ret)
		log_err("init platform driver fail, ret %d\n", ret);
	return 0;
}

void moa_cfgdev_driver_exit(void)
{
	return platform_driver_unregister(&moa_cfg_driver);
}

module_init(moa_cfgdev_driver_init);
module_exit(moa_cfgdev_driver_exit);
