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

struct moa_binderlike_queue_cap {
  int sq_offset;
  int cq_offset;
  int memblk_size;
};

struct moa_binderlike_msg {
  char content[256];
};

struct moa_binderlike_queue {
  int head;
  int tail;
  int total_len;
  struct moa_binderlike_msg msgs[];
};

struct moa_binderlike_chan;

typedef int (*dqMsg)(struct moa_binderlike_chan *chan, char *buf, size_t len);
typedef int (*qMsg)(struct moa_binderlike_chan *chan, char *buf, size_t len);

struct moa_binderlike_chan {
	int fd;
	struct moa_binderlike_queue *sq;
	struct moa_binderlike_queue *cq;
	struct moa_binderlike_queue_cap cap;
	dqMsg dequeue;
	qMsg queue;
};

#define DEV_NAME "/dev/moa_binderlike"
#define MOA_BINDERIOC_GET_QUEUE _IOR('B', 0, struct moa_binderlike_queue_cap)

int Msg_Dequeue(struct moa_binderlike_chan *chan, char *buf, size_t sz);

void binderlike_chan_release(struct moa_binderlike_chan *chan)
{
	int ret = 0;
	if (chan)
	{
		ret = 0;
	}

	if (!ret && chan->fd > 0)
	{
		if (chan->cap.memblk_size > 0 &&
			chan->cap.cq_offset > 0)
		{
			ret = 0;
		}
		else
		{
			ret = -ENODEV;
			close(chan->fd);
		}
	}

	if (!ret)
	{
		munmap(chan->sq, chan->cap.cq_offset);
		munmap(chan->cq, chan->cap.memblk_size - chan->cap.cq_offset);
	}

	if (!ret)
	{
		close(chan->fd);
		free(chan);
	}

	printf("binderlike release\n");
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
		struct moa_binderlike_queue_cap cap;
		ret = ioctl(chan->fd, MOA_BINDERIOC_GET_QUEUE, &cap);
		if (ret)
		{
			printf("get queue cap failed\n");
		}
		else
		{
			chan->cap = cap;
                        printf("record cap %d %d %d\n", cap.sq_offset,
                               cap.cq_offset, cap.memblk_size);
                }
        }

	if (!ret)
	{
		void *addr = NULL;
                addr = mmap(NULL, chan->cap.memblk_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, chan->fd, 0);
                if (addr) {
                        chan->sq = (struct moa_binderlike_queue *)addr;
                        printf("sq %p created, h %d, t %d, len %d\n", chan->sq,
                               chan->sq->head, chan->sq->tail,
                               chan->sq->total_len);

                        chan->cq = (struct moa_binderlike_queue *)(addr +
                                   chan->cap.cq_offset);
                        printf("cq %p created, h %d, t %d, len %d\n", chan->cq,
                               chan->cq->head, chan->cq->tail,
                               chan->cq->total_len);
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

int dq_msg(struct moa_binderlike_queue *q, char *buf, size_t sz)
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
		q->head = (q->head + 1) % q->total_len;
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
		sz = dq_msg(chan->sq, buf, sz);
		ret = sz < 0 ? sz : 0;
	}

	return ret;
}

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
