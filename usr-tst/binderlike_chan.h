#include <sys/types.h>
#include "../binderlike/binderlike-core.h"

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
