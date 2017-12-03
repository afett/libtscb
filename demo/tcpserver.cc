/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <atomic>

#include <tscb/ioready>

class echo {
public:
	echo(tscb::ioready_service * service, int fd);
	~echo(void);

private:
	void data(int event);
	void destroy(void);

	int fd_;
	tscb::ioready_connection link_;
	tscb::ioready_service *service_;
	std::atomic_int refcount_;

	friend inline void intrusive_ptr_add_ref(echo * e) noexcept
	{
		e->refcount_.fetch_add(1, std::memory_order_relaxed);
	}
	friend inline void intrusive_ptr_release(echo * e) noexcept
	{
		if (e->refcount_.fetch_sub(1, std::memory_order_relaxed) == 1) {
			std::atomic_thread_fence(std::memory_order_release);
			delete e;
		}
	}
};

class acceptor {
public:
	acceptor(tscb::ioready_service * service, int fd);

private:
	void connection_request(int event);

	int fd_;
	tscb::ioready_connection link_;
	tscb::ioready_service * service_;
	std::atomic_int refcount_;

	friend inline void intrusive_ptr_add_ref(acceptor * a) noexcept
	{
		a->refcount_.fetch_add(1, std::memory_order_relaxed);
	}
	friend inline void intrusive_ptr_release(acceptor * a) noexcept
	{
		if (a->refcount_.fetch_sub(1, std::memory_order_relaxed) == 1) {
			std::atomic_thread_fence(std::memory_order_release);
			delete a;
		}
	}
};

echo::echo(tscb::ioready_service *service, int fd)
	: fd_(fd), service_(service), refcount_(0)
{
	int flags = fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);

	link_ = service->watch(std::bind(&echo::data, tscb::intrusive_ptr<echo>(this), std::placeholders::_1),
		fd, tscb::ioready_input);
}

echo::~echo(void)
{
	printf("connection closed\n");
	::close(fd_);
}

void echo::data(int event)
{
	char buffer[16384];
	ssize_t n;
	do {
		n = ::read(fd_, buffer, 16384);
		if (n < 0 && errno == EAGAIN) {
			break;
		}
		if (n <= 0) {
			printf("connection closed by client\n");
			link_.disconnect();
			break;
		}
		::write(1, buffer, n);
		::write(fd_, buffer, n);
	} while (n == 16384);
}

acceptor::acceptor(tscb::ioready_service *service, int fd)
	: fd_(fd), service_(service), refcount_(0)
{
	int flags=fcntl(fd_, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd_, F_SETFL, flags);

	link_ = service->watch(std::bind(&acceptor::connection_request,
		tscb::intrusive_ptr<acceptor>(this), std::placeholders::_1), fd, tscb::ioready_input);
}

void acceptor::connection_request(int event)
{
	int s = ::accept(fd_, 0, 0);
	while (s >= 0) {
		new echo(service_, s);
		s = accept(fd_, 0, 0);
	}
}

int main(int argc, char **argv)
{

	int sock = socket(PF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family=AF_INET;
	addr.sin_port=htons(1234);
	addr.sin_addr.s_addr=inet_addr("0.0.0.0");

	int reuse_flag = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(reuse_flag));
	bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(reuse_flag));
	listen(sock, 25);

	tscb::ioready_dispatcher * dispatcher=tscb::ioready_dispatcher::create();

	new acceptor(dispatcher, sock);

	for(;;) {
		dispatcher->dispatch(0);
	}

	return 0;
}
