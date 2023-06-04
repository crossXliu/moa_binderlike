#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

#define DEV_NAME "/dev/moa_binderlike"
#define MOA_BINDERIOC_GET_QUEUE _IOR('B', 0, struct moa_binderlike_queue_cap)

int dq_msg(struct moa_binderlike_queue *q)
{
	char buf[256];
	size_t sz;
	if (q->head == q->tail)
		return -ENOTTY;

	sz = snprintf(buf, sizeof(buf), "%s", q->msgs[q->head].content);
	if (buf[sz - 1] == '\n')
		buf[sz - 1] = '\0';
	q->head = (q->head + 1) % q->total_len;

	printf("dq result: \"%s\"\n", buf);

	return sz;
}

int main(int argc, char *argv[]) {
  int fd = -1;
  void *addr;
  struct moa_binderlike_queue_cap cap;
  struct moa_binderlike_queue *sq, *cq;
  printf("enter ++\n");

  fd = open(DEV_NAME, O_RDWR | O_NONBLOCK, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    printf(" open file %s failed \n", DEV_NAME);
    return -EFAULT;
  }

  if (ioctl(fd, MOA_BINDERIOC_GET_QUEUE, &cap) < 0) {
    perror(" ioctl failed\n");
  }

  printf(" [sq len %d, cq len %d] \n", cap.cq_offset,
         cap.memblk_size - cap.cq_offset);

  addr = mmap(NULL, cap.memblk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    perror("mmap failed");
    close(fd);
    return -EFAULT;
  }

  sq = (struct moa_binderlike_queue *)(addr + cap.sq_offset);
  cq = (struct moa_binderlike_queue *)(addr + cap.cq_offset);

  printf(" sq [%d, %d, %d] cq [%d, %d, %d]\n", sq->head, sq->tail,
         sq->total_len, cq->head, cq->tail, cq->total_len);

  while (dq_msg(sq) > 0) {
  }
  printf("exit --\n");
  return 0;
}
