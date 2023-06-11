#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "../binderlike/binderlike-core.h"

struct moa_binderlike_chan;

typedef int (*dqMsg)(struct moa_binderlike_chan *chan, char *buf, size_t len);
typedef int (*qMsg)(struct moa_binderlike_chan *chan, char *buf, size_t len);

struct moa_binderlike_chan {
	int fd;
	struct moa_binderlike_queue *sq;
	struct moa_binderlike_queue *cq;
	struct moa_binderlike_chan_info info;
	dqMsg dequeue;
	qMsg queue;
};

#define DEV_NAME "/dev/moa_binderlike"
#define MOA_BINDERIOC_GET_QUEUE _IOR('B', 0, struct moa_binderlike_queue_cap)

int Msg_Dequeue(struct moa_binderlike_chan *chan, char *buf, size_t sz);

void binderlike_chan_release(struct moa_binderlike_chan *chan)
{
	printf("binderlike release\n");
}

static inline void
dump_binderlike_chan_info(const struct moa_binderlike_chan_info *info)
{
        printf("info [id %d, cache_cnt(%u), mmap_sz(%u), cq_offset(%u)]\n",
               info->id, info->cache_cnt, info->mmap_sz, info->cq_offset);
        return;
}

struct moa_binderlike_chan *binderlike_create_instance(void)
{
	int ret = 0;
	struct moa_binderlike_chan *chan;

	chan = malloc(sizeof(*chan));
	if (chan)
	{
		int fd = -1;
		memset((void *)chan, 0, sizeof(*chan));
		fd = open(DEV_NAME, O_RDWR | O_NONBLOCK, S_IRUSR | S_IWUSR);
		ret = fd > 0 ? 0 : -ENOTTY;
		chan->fd = fd;
	}

	if (!ret)
	{
		struct moa_binderlike_chan_info *info;

		info = &chan->info;

		info->id = 0;
		ret = ioctl(chan->fd, MOA_BINDERIOC_CREATE_CHAN, &info);
		if (ret)
		{
			perror("get queue cap failed\n");
		}
		else
		{
			dump_binderlike_chan_info(info);
                }
        }

	if (!ret)
	{
		void *addr = NULL;
                addr = mmap(NULL, chan->info.mmap_sz, PROT_READ | PROT_WRITE,
                            MAP_SHARED, chan->fd, 0);
                if (addr) {
                        chan->sq = (struct moa_binderlike_queue *)addr;
                        printf("sq %p created, h %d, t %d, len %d\n", chan->sq,
                               chan->sq->head, chan->sq->tail,
                               chan->info.cache_cnt);

                        chan->cq = (struct moa_binderlike_queue *)(addr +
                                   chan->info.cq_offset);
                        printf("cq %p created, h %d, t %d, len %d\n", chan->cq,
                               chan->cq->head, chan->cq->tail,
                               chan->info.cache_cnt);
                } else {
                        perror("mmap submit queue failed\n");
			ret = -ENOMEM;
                }
        }


	if (!ret)
	{
		chan->dequeue = Msg_Dequeue;
		chan->queue = NULL;
	}

	if (ret < 0)
	{
		binderlike_chan_release(chan);
	}

	return chan;
}

int dq_msg(struct moa_binderlike_queue *q, char *buf, size_t sz,
           unsigned int cache_cnt)
{
        int ret = 0;
        if (q->head == q->tail)
	{
		ret = -ENOTTY;
	}

	if (!ret)
	{
		sz = snprintf(buf, sz, "%s", q->msgs[q->head].content);
		if (buf[sz - 1] == '\n')
			buf[sz - 1] = '\0';
		q->head = (q->head + 1) % cache_cnt;
	}

	return sz;
}

int qmsg(struct moa_binderlike_queue *q, char *buf, size_t sz)
{
	// TODO: to be implememted
	return 0;
}

int Msg_Dequeue(struct moa_binderlike_chan *chan, char *buf, size_t sz)
{
	int ret = 0;
	if (!chan ||
	    !buf ||
	    !chan->sq)
	{
		ret = -EINVAL;
	}

	if (!ret)
	{
		size_t sz;
		sz = dq_msg(chan->sq, buf, sz, chan->info.cache_cnt);
		ret = sz;
	}
	return ret;
}
typedef struct __binderlike_param {
	/// TODO
} binderlike_param_t;

typedef void (*binderlike_fn)(binderlike_param_t* param);

typedef struct __binderlike_chan_create_info {
	int                                   id;
	binderlike_fn                         fn;
	int                                   cache_cnt;
	int                                   param_sz;
} binderlike_chan_create_info_t;

typedef struct __binderlike_chan_desc {
	int id;
} binderlike_chan_desc_t;

typedef binderlike_chan_desc_t* (*create_chan)(binderlike_chan_create_info_t* info);

typedef void (*daemonize)(void);

typedef struct __app_daemon {
	create_chan           create_binderlike_chan;
	daemonize             app_daemonize;
} app_daemon_t;

static struct moa_binderlike_chan* chan_map[16];

static inline struct moa_binderlike_chan* rechieve_chan_by_id(int id)
{
	if (id >= 16 || id < 0)
		return NULL;
	return chan_map[id];
}

static binderlike_chan_desc_t* App_Binderlike_Chan_Create(binderlike_chan_create_info_t* info)
{
	int ret = 0;
	binderlike_chan_desc_t* pChanDesc = malloc(sizeof(*pChanDesc));
	struct moa_binderlike_chan *chan;

	if (NULL == pChanDesc)
	{
		ret = -ENOMEM;
	}

	if (0 == ret)
	{
		/// add info into create instance
		chan = binderlike_create_instance();
	}

	if (0 == ret)
	{
		pChanDesc->id = chan->info.id;
		chan_map[pChanDesc->id] = chan;
	}
	return pChanDesc;
}

/* just use in main test, keep it for reference */
#if 0
int main(int argc, char *argv[])
{
	int ret = 0;
	struct moa_binderlike_chan *chan;
	char buf[256];

	chan = binderlike_create_instance();
	if (!chan)
	{
		ret = -ENODEV;
	}

	if (!ret)
	{
		ret = chan->dequeue(chan, buf, sizeof(buf));
	}

	if (!ret)
	{
		printf("dq buf: \"%s\"\n", buf);
	}
	else
	{
		perror("dequeue msg failed\n");
	}
	return ret;
}
#endif
