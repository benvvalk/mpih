#ifndef _INIT_H_
#define _INIT_H_

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include <mpi.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>

static const char INIT_USAGE_MESSAGE[] =
"Usage: mpi init <socket_path>\n"
"\n"
"Description:\n"
"\n"
"   Initialize the current MPI rank and start a daemon\n"
"   that will listen for commands issued from the\n"
"   shell (e.g. 'mpi send').\n"
"\n"
"   Communication between clients (mpi commands) and\n"
"   the daemon occurs over a Unix domain socket located at\n"
"   <socket_path>.  <socket_path> does not need to\n"
"   exist prior to running 'mpi init'. If <socket_path>\n"
"   does exist the daemon will delete the file and recreate\n"
"   it.\n"
"\n"
"Options:\n"
"\n"
"   (none)\n";

namespace mpi {
	int rank;
	int numProc;
}

static const char init_shortopts[] = "hv";

static const struct option init_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

enum ConnectionState {
	READING_COMMAND=0,
	RECEIVING_DATA,
	SENDING_DATA,
	CLOSED
};

#define MAX_READ_SIZE 16384

void do_command(const char* line, struct bufferevent *bev)
{
	printf("Received command: '%s'\n", line);
}

void readcb(struct bufferevent *bev, void *arg)
{
	ConnectionState state = *(ConnectionState*)arg;
	struct evbuffer *input, *output;
	input = bufferevent_get_input(bev);
	output = bufferevent_get_output(bev);

	if (state == READING_COMMAND) {
		size_t n;
		char* line = evbuffer_readln(input, &n, EVBUFFER_EOL_LF);
		if (line != NULL) {
			do_command(line, bev);
			free(line);
		} else if (evbuffer_get_length(input) >= MAX_READ_SIZE) {
			fprintf(stderr, "client command exceeded max length\n");
			free(arg);
			bufferevent_free(bev);
			exit(EXIT_FAILURE);
		}
	}
}

void
errorcb(struct bufferevent *bev, short error, void *ctx)
{
	if (error & BEV_EVENT_EOF) {
		/* connection has been closed, do any clean up here */
		/* ... */
	} else if (error & BEV_EVENT_ERROR) {
		/* check errno to see what error occurred */
		/* ... */
	} else if (error & BEV_EVENT_TIMEOUT) {
		/* must be a timeout event handle, handle it */
		/* ... */
	}
	bufferevent_free(bev);
}

void
do_accept(evutil_socket_t listener, short event, void *arg)
{
	struct event_base *base = (event_base*)arg;
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	int fd = accept(listener, (struct sockaddr*)&ss, &slen);
	if (fd < 0) {
		perror("accept");
	} else if (fd > FD_SETSIZE) {
		close(fd);
	} else {
		struct bufferevent *bev;

		ConnectionState* state = (ConnectionState *)
			malloc(sizeof(ConnectionState));
		*state = READING_COMMAND;

		evutil_make_socket_nonblocking(fd);
		bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
		bufferevent_setcb(bev, readcb, NULL, errorcb, state);
		bufferevent_setwatermark(bev, EV_READ, 0, MAX_READ_SIZE);
		bufferevent_enable(bev, EV_READ|EV_WRITE);
	}
}

void serverLoop(const char* socketPath)
{
	evutil_socket_t listener;
	struct sockaddr_un local;
	struct event_base *base;
	struct event *listener_event;

	base = event_base_new();
	if (!base)
		return; /*XXXerr*/

	listener = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(listener > 0);

	evutil_make_socket_nonblocking(listener);

	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, socketPath);
	unlink(local.sun_path);
	int len = strlen(local.sun_path) + sizeof(local.sun_family);

	if (bind(listener, (struct sockaddr*)&local, len) < 0) {
		perror("bind");
		return;
	}

	if (listen(listener, 16)<0) {
		perror("listen");
		return;
	}

	listener_event = event_new(base, listener, EV_READ|EV_PERSIST,
			do_accept, (void*)base);

	/*XXX check it */
	event_add(listener_event, NULL);

	event_base_dispatch(base);
}

int cmd_init(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		init_shortopts, init_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(INIT_USAGE_MESSAGE);
		  case 'h':
			std::cout << INIT_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi init: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(INIT_USAGE_MESSAGE);
		}
	}

	// make sure a socket path is given (and nothing else)
	if (argc - optind != 1)
		die(INIT_USAGE_MESSAGE);

	// turn off buffering on stdout/stderr
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	serverLoop(argv[optind]);

	MPI_Finalize();

	return 0;
}

#endif
