/*
 * Description: run various nop tests
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/eventfd.h>

#include "liburing.h"

int main(int argc, char *argv[])
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	uint64_t ptr;
	struct iovec vec = {
		.iov_base = &ptr,
		.iov_len = sizeof(ptr)
	};
	int ret, evfd, i;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		fprintf(stderr, "ring setup failed: %d\n", ret);
		return 1;
	}

	evfd = eventfd(0, EFD_CLOEXEC);
	if (evfd < 0) {
		perror("eventfd");
		return 1;
	}

	ret = io_uring_register_eventfd(&ring, evfd);
	if (ret) {
		fprintf(stderr, "failed to register evfd: %d\n", ret);
		return 1;
	}

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_poll_add(sqe, evfd, POLLIN);
	sqe->flags |= IOSQE_IO_LINK;
	sqe->user_data = 1;

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_readv(sqe, evfd, &vec, 1, 0);
	sqe->flags |= IOSQE_IO_LINK;
	sqe->user_data = 2;

	ret = io_uring_submit(&ring);
	if (ret != 2) {
		fprintf(stderr, "submit: %d\n", ret);
		return 1;
	}

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_nop(sqe);
	sqe->user_data = 3;

	ret = io_uring_submit(&ring);
	if (ret != 1) {
		fprintf(stderr, "submit: %d\n", ret);
		return 1;
	}

	for (i = 0; i < 3; i++) {
		ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret) {
			fprintf(stderr, "wait: %d\n", ret);
			return 1;
		}
		switch (cqe->user_data) {
		case 1:
			/* POLLIN */
			if (cqe->res != 1) {
				fprintf(stderr, "poll: %d\n", cqe->res);
				return 1;
			}
			break;
		case 2:
			if (cqe->res != sizeof(ptr)) {
				fprintf(stderr, "read: %d\n", cqe->res);
				return 1;
			}
			break;
		case 3:
			if (cqe->res) {
				fprintf(stderr, "nop: %d\n", cqe->res);
				return 1;
			}
			break;
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	return 0;
}
