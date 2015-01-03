#ifndef _RECV_H_
#define _RECV_H_

#include "config.h"
#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include "Command/client/event_handlers.h"
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <sys/stat.h>
#include <fcntl.h>

static const char RECV_USAGE_MESSAGE[] =
"Usage: " PROGRAM_NAME " [--socket <path>] recv <rank> [file1]...\n"
"\n"
"Description:\n"
"\n"
"   Receive data from <rank> of current MPI job and\n"
"   stream to STDOUT.\n"
"\n"
"Options:\n"
"\n"
"   -s,--socket PATH   connect to 'mpi init' daemon\n"
"                      through Unix socket at PATH\n";

static const char recv_shortopts[] = "hv";

static const struct option recv_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

static inline void
recv_read_handler(struct bufferevent* bev, void* arg)
{
	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	struct evbuffer* input = bufferevent_get_input(bev);
	assert(input != NULL);

	size_t bytes_ready = evbuffer_get_length(input);
	assert(bytes_ready > 0);

	int n = evbuffer_write(input, STDOUT_FILENO);

	if (n < 0) {
		perror("evbuffer_write");
		bufferevent_free(bev);
		event_base_free(base);
		exit(EXIT_FAILURE);
	}

	assert(n == bytes_ready);
}

int cmd_recv(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		recv_shortopts, recv_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(RECV_USAGE_MESSAGE);
		  case 'h':
			std::cout << RECV_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi recv: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(RECV_USAGE_MESSAGE);
		}
	}

	if (argc - optind < 1) {
		std::cerr << "error: missing <rank> argument"
			<< std::endl;
		die(RECV_USAGE_MESSAGE);
	}

	int rank;
	std::stringstream ss(argv[optind++]);
	ss >> rank;
	if (ss.fail() || !ss.eof())
		die(RECV_USAGE_MESSAGE);

	if (opt::verbose)
		std::cerr << "Connecting to 'mpih init' process..."
			<< std::endl;

	int socket = UnixSocket::connect(opt::socketPath.c_str());

	if (opt::verbose)
		std::cerr << "Connected." << std::endl;

	struct event_base* base = event_base_new();
	assert(base != NULL);

	struct bufferevent* bev = bufferevent_socket_new(base,
		socket, BEV_OPT_CLOSE_ON_FREE);
	assert(bev != NULL);

	bufferevent_setcb(bev, recv_read_handler, NULL,
		client_event_handler, NULL);
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_BUFFER_SIZE);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	struct evbuffer* output = bufferevent_get_output(bev);
	assert(output != NULL);

	// send command to 'mpi init' daemon
	evbuffer_add_printf(output, "RECV %d\n", rank);

	// start libevent loop
	event_base_dispatch(base);

	fclose(stdout);
	event_base_free(base);

	return 0;
}

#endif
