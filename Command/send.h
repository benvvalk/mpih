#ifndef _SEND_H_
#define _SEND_H_

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

static const char SEND_USAGE_MESSAGE[] =
"Usage: mpi [--socket <path>] send <rank> [file1]...\n"
"\n"
"Description:\n"
"\n"
"   Stream data to <rank> of current MPI job.\n"
"   If no file arguments are specified, data is\n"
"   read from STDIN.\n"
"\n"
"Options:\n"
"\n"
"   -s,--socket PATH   connect to 'mpi init' daemon\n"
"                      through Unix socket at PATH\n";

static const char send_shortopts[] = "hv";

static const struct option send_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

static inline void
send_write_handler(struct bufferevent* bev, void* arg)
{
	assert(arg != NULL);
	std::vector<FILE*>& input_files = *(std::vector<FILE*>*)arg;

	struct event_base* base = bufferevent_get_base(bev);
	assert(base != NULL);

	struct evbuffer* output = bufferevent_get_output(bev);
	assert(output != NULL);

	if (input_files.empty()) {
		assert(evbuffer_get_length(output) == 0);
		event_base_loopexit(base, NULL);
		return;
	}

	FILE* file = input_files.back();
	assert(file != NULL);

	const int READ_SIZE = 32768;
	char buffer[READ_SIZE];

	int n = fread(buffer, 1, READ_SIZE, file);

	if (n < 0) {
		perror("fread");
		bufferevent_free(bev);
		event_base_free(base);
		exit(EXIT_FAILURE);
	}

	if (n > 0)
		evbuffer_add(output, buffer, n);

	// EOF
	if (n < READ_SIZE) {
		fclose(file);
		input_files.pop_back();
	}
}

int cmd_send(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		send_shortopts, send_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(SEND_USAGE_MESSAGE);
		  case 'h':
			std::cout << SEND_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi send: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(SEND_USAGE_MESSAGE);
		}
	}

	if (argc - optind < 1) {
		std::cerr << "error: missing <rank> argument"
			<< std::endl;
		die(SEND_USAGE_MESSAGE);
	}

	int rank;
	std::stringstream ss(argv[optind++]);
	ss >> rank;
	if (ss.fail() || !ss.eof())
		die(SEND_USAGE_MESSAGE);

	if (opt::verbose)
		std::cerr << "Connecting to 'mpih init' process..."
			<< std::endl;

	int socket = UnixSocket::connect(opt::socketPath.c_str());

	if (opt::verbose)
		std::cerr << "Connected." << std::endl;

	std::vector<FILE *> input_files;

	if (argc - optind == 0) {
		input_files.push_back(stdin);
	} else {
		for (int i = argc - 1; i >= optind; --i) {
			FILE* file = fopen(argv[optind], "rb");
			if (file == NULL)
				perror("fopen");
			input_files.push_back(file);
		}
	}

	struct event_base* base = event_base_new();
	assert(base != NULL);

	struct bufferevent* bev = bufferevent_socket_new(base,
		socket, BEV_OPT_CLOSE_ON_FREE);
	assert(bev != NULL);

	bufferevent_setcb(bev, NULL, send_write_handler,
		client_event_handler, (void*)&input_files);
	bufferevent_setwatermark(bev, EV_READ, 0, MAX_BUFFER_SIZE);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

	struct evbuffer* output = bufferevent_get_output(bev);
	assert(output != NULL);

	// send command to 'mpi init' daemon
	evbuffer_add_printf(output, "SEND %d\n", rank);

	// start libevent loop
	event_base_dispatch(base);

	bufferevent_free(bev);
	event_base_free(base);

	return 0;
}

#endif
