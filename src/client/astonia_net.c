/*
 * Part of Astonia Client. Please read license.txt.
 *
 * Plain C version of the astonia_net library (see include/astonia_net.h).
 *
 * Non-blocking TCP client socket on top of the operating system's sockets
 * (Winsock 2 on Windows, BSD sockets elsewhere), so the client builds without
 * Rust. Same functions and return values as the Rust astonia_net crate.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK      INVALID_SOCKET
#define close_sock    closesocket
#define sock_errno()  WSAGetLastError()
#define ERR_INPROGRESS WSAEWOULDBLOCK
typedef int addrlen_t;
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK      (-1)
#define close_sock    close
#define sock_errno()  errno
#define ERR_INPROGRESS EINPROGRESS
typedef socklen_t addrlen_t;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astonia_net.h"

#define POLL_READ  1
#define POLL_WRITE 2

struct astonia_sock {
	sock_t fd;
	int connecting;
};

#ifdef _WIN32
static int net_startup(void)
{
	static int started;
	WSADATA wsa;

	if (started) {
		return 0;
	}
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return -1;
	}
	started = 1;
	return 0;
}
#else
static int net_startup(void)
{
	return 0;
}
#endif

static int set_nonblocking(sock_t fd)
{
#ifdef _WIN32
	u_long on = 1;
	return ioctlsocket(fd, FIONBIO, &on) == 0 ? 0 : -1;
#else
	int fl = fcntl(fd, F_GETFL, 0);
	return (fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0) ? 0 : -1;
#endif
}

// Wait up to timeout_ms (<0 = forever) for the socket to be readable and/or
// writable. Returns POLL_READ/POLL_WRITE bits, 0 on timeout, -1 on error.
// A failed non-blocking connect shows up in the except set on Windows and as
// writable elsewhere; both are reported as -1 by check_connect() below.
static int wait_sock(sock_t fd, int want, int timeout_ms, int *failed)
{
	fd_set rd, wr, ex;
	struct timeval tv, *tvp = NULL;
	int n, res = 0;

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_ZERO(&ex);
	if (want & POLL_READ) {
		FD_SET(fd, &rd);
	}
	if (want & POLL_WRITE) {
		FD_SET(fd, &wr);
	}
	FD_SET(fd, &ex);

	if (timeout_ms >= 0) {
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		tvp = &tv;
	}

#ifdef _WIN32
	n = select(0, &rd, &wr, &ex, tvp);
#else
	do {
		n = select(fd + 1, &rd, &wr, &ex, tvp);
	} while (n < 0 && errno == EINTR);
#endif
	if (n < 0) {
		return -1;
	}
	if (FD_ISSET(fd, &rd)) {
		res |= POLL_READ;
	}
	if (FD_ISSET(fd, &wr)) {
		res |= POLL_WRITE;
	}
	if (failed) {
		*failed = FD_ISSET(fd, &ex) ? 1 : 0;
	}
	return res;
}

// Finish a pending connect. Returns 1 = connected, 0 = still pending, -1 = failed.
static int check_connect(struct astonia_sock *s, int timeout_ms)
{
	int failed = 0, err = 0;
	socklen_t len = sizeof(err);

	if (!s->connecting) {
		return 1;
	}

	int r = wait_sock(s->fd, POLL_WRITE, timeout_ms, &failed);
	if (r < 0) {
		return -1;
	}
	if (!failed && !(r & POLL_WRITE)) {
		return 0;
	}
	if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err != 0 || failed) {
		return -1;
	}
	s->connecting = 0;
	return 1;
}

astonia_sock *astonia_net_connect(const char *host, uint16_t port, int timeout_ms)
{
	struct addrinfo hints, *res = NULL;
	char portstr[8];
	struct astonia_sock *s;
	sock_t fd;

	if (!host || !*host || net_startup() != 0) {
		return NULL;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
	if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
		return NULL;
	}

	// Prefer IPv4 (the game servers are IPv4; "localhost" often resolves to ::1 first),
	// otherwise use the first address the resolver returns.
	struct addrinfo *ai = res;
	for (struct addrinfo *p = res; p; p = p->ai_next) {
		if (p->ai_family == AF_INET) {
			ai = p;
			break;
		}
	}
	fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (fd == BAD_SOCK) {
		freeaddrinfo(res);
		return NULL;
	}
	if (set_nonblocking(fd) != 0) {
		close_sock(fd);
		freeaddrinfo(res);
		return NULL;
	}
	if (connect(fd, ai->ai_addr, (addrlen_t)ai->ai_addrlen) != 0 && sock_errno() != ERR_INPROGRESS) {
		close_sock(fd);
		freeaddrinfo(res);
		return NULL;
	}
	freeaddrinfo(res);

	s = calloc(1, sizeof(*s));
	if (!s) {
		close_sock(fd);
		return NULL;
	}
	s->fd = fd;
	s->connecting = 1;

	// timeout 0: leave the connect pending, astonia_net_poll() finishes it.
	if (timeout_ms != 0 && check_connect(s, timeout_ms) < 0) {
		astonia_net_close(s);
		return NULL;
	}
	return s;
}

int astonia_net_poll(astonia_sock *s, int mask, int timeout_ms)
{
	int r;

	if (!s) {
		return -1;
	}
	if (mask == 0) {
		return 0;
	}

	if (s->connecting) {
		r = check_connect(s, timeout_ms);
		if (r < 0) {
			return -1;
		}
		if (r == 0) {
			return 0;
		}
		// connected: report readiness right away, without waiting again
		timeout_ms = 0;
	}

	r = wait_sock(s->fd, mask & (POLL_READ | POLL_WRITE), timeout_ms, NULL);
	return r < 0 ? -1 : r;
}

ptrdiff_t astonia_net_recv(astonia_sock *s, void *dst, size_t cap)
{
	if (!s) {
		return -1;
	}
	if (!dst || cap == 0) {
		return 0;
	}
	if (cap > 0x7fffffff) {
		cap = 0x7fffffff;
	}
#ifdef _WIN32
	int n = recv(s->fd, (char *)dst, (int)cap, 0);
#else
	ssize_t n;
	do {
		n = recv(s->fd, dst, cap, 0);
	} while (n < 0 && errno == EINTR);
#endif
	return n >= 0 ? (ptrdiff_t)n : -1;
}

ptrdiff_t astonia_net_send(astonia_sock *s, const void *src, size_t len)
{
	if (!s) {
		return -1;
	}
	if (!src || len == 0) {
		return 0;
	}
	if (len > 0x7fffffff) {
		len = 0x7fffffff;
	}
#ifdef _WIN32
	int n = send(s->fd, (const char *)src, (int)len, 0);
#else
#ifdef MSG_NOSIGNAL
	int flags = MSG_NOSIGNAL;
#else
	int flags = 0;
#endif
	ssize_t n;
	do {
		n = send(s->fd, src, len, flags);
	} while (n < 0 && errno == EINTR);
#endif
	return n >= 0 ? (ptrdiff_t)n : -1;
}

static int addr_ipv4(const struct sockaddr_storage *ss, uint32_t *out_be)
{
	if (!out_be || ss->ss_family != AF_INET) {
		return -1;
	}
	*out_be = ((const struct sockaddr_in *)ss)->sin_addr.s_addr; // already network byte order
	return 0;
}

int astonia_net_local_ipv4(astonia_sock *s, uint32_t *out_be)
{
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);

	if (!s || getsockname(s->fd, (struct sockaddr *)&ss, &len) != 0) {
		return -1;
	}
	return addr_ipv4(&ss, out_be);
}

int astonia_net_peer_ipv4(astonia_sock *s, uint32_t *out_be)
{
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);

	if (!s || getpeername(s->fd, (struct sockaddr *)&ss, &len) != 0) {
		return -1;
	}
	return addr_ipv4(&ss, out_be);
}

void astonia_net_close(astonia_sock *s)
{
	if (!s) {
		return;
	}
	if (s->fd != BAD_SOCK) {
		close_sock(s->fd);
	}
	free(s);
}
