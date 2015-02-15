#ifndef _INIT_H_
#define _INIT_H_

#include "config.h"
#include "Options/CommonOptions.h"
#include "Command/init/log.h"
#include "Command/init/Connection.h"
#include "Command/init/mpi.h"
#include "Command/init/event_handlers.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include "Env/env.h"
#include <mpi.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <signal.h>
#include <unistd.h>

static const char INIT_USAGE_MESSAGE[] =
"Usage: " PROGRAM_NAME " [--socket <path>] init [options]\n"
"\n"
"Description:\n"
"\n"
"   Start a daemon that will listen for MPI commands\n"
"   on the Unix socket file at <path>. <path> must be\n"
"   specified using either the MPIH_SOCKET environment\n"
"   variable or the --socket option, with --socket\n"
"   taking precedence. <path> does not need to\n"
"   exist prior to running 'mpi init'. If <path> does\n"
"   exist, the file will be deleted and recreated\n"
"   by the daemon.\n"
"\n"
"   The normal way to issue commands to the daemon is\n"
"   to run other 'mpih' commands (e.g. 'mpih send') with\n"
"   the same --socket option (or MPIH_SOCKET value).\n"
"\n"
"Options:\n"
"\n"
"   -f,--foreground      run daemon in the foreground\n"
"   -l,--log PATH        log file [/dev/null]\n"
"   -p,--pid-file PATH   file containing PID of daemon;\n"
"                        existence of this file indicates\n"
"                        that the daemon is running and is\n"
"                        ready to accept commands from\n"
"                        clients\n"
"   -s,--socket PATH     communicate over Unix socket\n"
"                        at PATH\n";

namespace opt {
	static int foreground;
	static std::string pidPath;
}

static const char init_shortopts[] = "fhl:p:v";

static const struct option init_longopts[] = {
	{ "foreground", no_argument, NULL, 'f' },
	{ "help",     no_argument, NULL, 'h' },
	{ "log",      required_argument, NULL, 'l' },
	{ "pid-file", required_argument, NULL, 'p' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

/**
 * Run the current process in the background.
 *
 * Note: This method does not perform all of
 * the typical steps for starting a daemon,
 * such as calling setsid(), forking twice,
 * or changing to the root ("/") directory.
 * This is intentional.
 *
 * In the case of 'mpi init', the
 * daemon to be killed if the shell script
 * aborted prematurely, and so I leave the
 * process group and the controlling terminal
 * of the child process unaltered.
 */
static inline void run_in_background()
{
	pid_t pid, sid;

	/*
	 * If a background process
	 * accidentally writes to STDOUT,
	 * it may be sent a SIGTTOU
	 * signal. The default behaviour is
	 * when receiving SIGTTOU is to supsend
	 * the process, which can cause
	 * confusion.
	 */

#ifdef SIGTTOU
	signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTTIN
	signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTSTP
	signal(SIGTSTP, SIG_IGN);
#endif

	pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);

	// exit parent process
	if (pid > 0)
		exit(EXIT_SUCCESS);

	// close all open file descriptors
	for (int i=getdtablesize(); i >= 0; --i)
		close(i);
}

static inline void create_pid_file(evutil_socket_t, short, void*)
{
	assert(!opt::pidPath.empty());
	std::ofstream pid_file(opt::pidPath.c_str());
	if (!pid_file) {
		perror("error opening pid file");
		exit(EXIT_FAILURE);
	}
	pid_file << getpid() << "\n";
	assert(pid_file);
	pid_file.close();
}

static inline void server_loop(const char* socketPath)
{
	// create Unix domain socket that listens for connections
	evutil_socket_t listener = UnixSocket::listen(socketPath, false);

	// main state object for libevent
	struct event_base* base = event_base_new();
	assert(base != NULL);

	// register handler for new connections
	struct event* listener_event = event_new(base, listener,
		EV_READ|EV_PERSIST, init_accept_handler, (void*)base);
	assert(listener_event != NULL);

	int result = event_add(listener_event, NULL);
	assert(result == 0);

	// event to create a PID file at startup.  This file acts
	// as a signal to clients that the daemon is running and
	// ready for requests.
	struct event* pid_file_event = NULL;
	if (!opt::pidPath.empty()) {
		pid_file_event = event_new(base, -1, 0, create_pid_file, NULL);
		assert(pid_file_event != NULL);
		event_active(pid_file_event, 0, 0);
	}

	if (opt::verbose)
		fprintf(g_log, "Listening for connections...\n");

	// start libevent loop
	event_base_dispatch(base);

	// cleanup
	event_free(listener_event);
	if (pid_file_event != NULL)
		event_free(pid_file_event);
	event_base_free(base);
}

static inline int cmd_init(int argc, char** argv)
{
	// parse command line options
	for (int c; (c = getopt_long(argc, argv,
		init_shortopts, init_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(INIT_USAGE_MESSAGE);
		  case 'f':
			opt::foreground = 1;
			break;
		  case 'h':
			std::cout << INIT_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'l':
			arg >> opt::logPath;
			break;
		  case 'p':
			arg >> opt::pidPath;
			break;
		  case 'v':
			opt::verbose++;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi init: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(INIT_USAGE_MESSAGE);
		}
	}

	if (opt::pidPath.empty() && getenv("MPIH_PIDFILE") != NULL)
		opt::pidPath = getenv("MPIH_PIDFILE");

	if (opt::logPath == "-" && !opt::foreground) {
		std::cerr << "error: cannot log to STDOUT ('-') "
			" unless --foreground option is used."
			<< std::endl;
		die(INIT_USAGE_MESSAGE);
	}

	if (opt::foreground && opt::logPath.empty())
		opt::logPath = "-";

	if (!opt::foreground)
		run_in_background();

	// initialize MPI
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	// start connection handling loop on Unix socket
	init_log();
	server_loop(opt::socketPath.c_str());
	close_log();

	// shutdown MPI
	MPI_Finalize();

	return 0;
}

#endif
