
#include "postgres.h"

#include "poll_compat.h"

#ifdef PLPROXY_POLL_COMPAT

/*
 * Emulate poll() with select()
 */

#include <sys/time.h>
#include <sys/select.h>

/*
 * dynamic buffer for fd_set to avoid depending on FD_SETSIZE
 */

struct fd_buf {
	fd_set *set;
	int alloc_bytes;
};

static void fdbuf_zero(struct fd_buf *buf)
{
	if (buf->set)
		memset(buf->set, 0, buf->alloc_bytes);
}

static bool fdbuf_resize(struct fd_buf *buf, int fd)
{
	/* get some extra room for quaranteed alignment */
	int need_bytes = fd/8 + 32;
	/* default - 2048 fds */
	int alloc = 256;
	uint8 *ptr;

	if (buf->alloc_bytes < need_bytes)
	{
		while (alloc < need_bytes)
			alloc *= 2;

		if (!buf->set)
			ptr = malloc(alloc);
		else
			ptr = realloc(buf->set, alloc);

		if (!ptr)
			return false;

		/* clean new area */
		memset(ptr + buf->alloc_bytes, 0, alloc - buf->alloc_bytes);

		buf->set = (fd_set *)ptr;
		buf->alloc_bytes = alloc;
	}
	return true;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
	static struct fd_buf readfds = { NULL, 0 };
	static struct fd_buf writefds = { NULL, 0 };

	struct pollfd *pf;
	int i, fd_max = 0;
	int res;
	struct timeval *tv = NULL;
	struct timeval tvreal;

	fdbuf_zero(&readfds);
	fdbuf_zero(&writefds);

	for (i = 0; i < nfds; i++)
	{
		pf = fds + i;
		if (pf->fd < 0)
			goto badf;

		/* sets must be equal size */
		if (!fdbuf_resize(&readfds, pf->fd))
			goto nomem;
		if (!fdbuf_resize(&writefds, pf->fd))
			goto nomem;

		if (pf->events & POLLIN)
			FD_SET(pf->fd, readfds.set);
		if (pf->events & POLLOUT)
			FD_SET(pf->fd, writefds.set);
		if (pf->fd > fd_max)
			fd_max = pf->fd;
	}

	if (timeout_ms >= 0)
	{
		tvreal.tv_sec = timeout_ms / 1000;
		tvreal.tv_usec = (timeout_ms % 1000) * 1000;
		tv = &tvreal;
	}

	res = select(fd_max + 1, readfds.set, writefds.set, NULL, tv);
	if (res <= 0)
		return res;

	for (i = 0; i < nfds; i++)
	{
		pf = fds + i;
		pf->revents = 0;
		if ((pf->events & POLLIN) && FD_ISSET(pf->fd, readfds.set))
			pf->revents |= POLLIN;
		if ((pf->events & POLLOUT) && FD_ISSET(pf->fd, writefds.set))
			pf->revents |= POLLOUT;
	}

	/* select() may count differently than poll()? */
	return res;

nomem:
	errno = ENOMEM;
	return -1;

badf:
	errno = EBADF;
	return -1;
}

#endif /* PLPROXY_POLL_COMPAT */

