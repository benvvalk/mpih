#ifndef _RUN_H_
#define _RUN_H_

#include "config.h"
#include "Command/init.h"
#include "Command/finalize.h"
#include "Command/rank.h"
#include "Command/size.h"
#include "Options/CommonOptions.h"
#include <stdlib.h>
#include <string>
#include <cstring>
#include <iostream>
#include <sstream>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char RUN_USAGE_MESSAGE[] =
"Usage: " PROGRAM_NAME " run <script> <script_args>\n"
"\n"
"Description:\n"
"\n"
"   Run <script> in a standard mpih environment.\n"
"\n"
"   The purpose of the 'mpih run' command is to make user scripts\n"
"   easier to write by automatically invoking standard set-up\n"
"   and tear-down commands before/after execution of scripts.\n"
"\n"
"   Prior to invoking <script>, 'mpih run' will start an 'mpi init'\n"
"   daemon for processing mpih commands and the following\n"
"   environment variables will be set:\n"
"\n"
"   MPIH_RANK     the MPI rank of the current process\n"
"   MPIH_SIZE     the number of ranks in the current MPI job\n"
"   MPIH_LOG      log file used by 'mpih init' daemon\n"
"   MPIH_SOCKET   Unix domain socket for communicating with\n"
"                 the 'mpih init' daemon\n"
"   MPIH_PIDFILE  file containing PID of 'mpih init' daemon;\n"
"                 existence of this file indicates that the\n"
"                 daemon is running and is ready to accept\n"
"                 requests.\n"
"\n"
"   Note: MPIH_SOCKET is used implicitly by the various mpih\n"
"   commands in order to communicate with the daemon, but is\n"
"   rarely needed by the user.\n"
"\n"
"   After <script> complete successfully, 'mpih finalize' will\n"
"   automatically be invoked to shut down the MPI process.\n"
"\n"
"Options:\n"
"\n"
"   -l,--log PATH     log file for daemon\n"
"   -v,--verbose      show progress messages\n"
"   -V,--log-verbose  verbose level for daemon log\n";

namespace opt {
	static int logVerbose = 1;
}

static const char run_shortopts[] = "hl:vV";

static const struct option run_longopts[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "log", required_argument, NULL, 'l' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "log-verbose", no_argument, NULL, 'V' },
	{ NULL, 0, NULL, 0 }
};

static inline int cmd_run(int argc, char** argv)
{
	/* parse command line options */
	for (int c; (c = getopt_long(argc, argv,
		run_shortopts, run_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(RUN_USAGE_MESSAGE);
		  case 'h':
			std::cout << RUN_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'l':
			arg >> opt::logPath;
			break;
		  case 'v':
			opt::verbose++;
			break;
		  case 'V':
			opt::logVerbose++;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi run: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(RUN_USAGE_MESSAGE);
		}
	}

	/* make temp dir for init daemon */
	std::string dir_template("/tmp");
	if (getenv("TMPDIR") != NULL)
		dir_template = getenv("TMPDIR");
	dir_template.append("/mpih.XXXXXX");
	char* tmpdir = strdup(dir_template.c_str());
	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		exit(EXIT_FAILURE);
	}

	/* set socket path for 'mpih init' daemon */
	opt::socketPath.append(tmpdir);
	opt::socketPath.append("/");
	opt::socketPath.append("socket");
	std::string socketStr("MPIH_SOCKET=");
	socketStr.append(opt::socketPath);

	/* pid file path for 'mpih init' daemon */
	opt::pidPath.append(tmpdir);
	opt::pidPath.append("mpih.pid");
	std::string pidStr("MPIH_PIDFILE=");
	pidStr.append(opt::pidPath);

	/* set log path for 'mpih init' daemon */
	if (opt::logPath.empty()) {
		opt::logPath.append(tmpdir);
		opt::logPath.append("/");
		opt::logPath.append("log");
	}
	std::string logStr("MPIH_LOG=");
	logStr.append(opt::logPath);

	if (opt::verbose)
		std::cerr << "setting daemon log path to "
			<< opt::logPath << std::endl;

	/* free memory allocated for string */
	free(tmpdir);

	/* fork an 'mpih init' daemon */
	int pid = fork();

	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		/* set verbose level for daemon log */
		opt::verbose = opt::logVerbose;
		/* invoke 'mpih init' with no args */
		char* empty_argv[1] = { NULL };
		cmd_init(0, empty_argv);
		/* should never reach this line */
		assert(false);
	}

	if (opt::verbose)
		std::cerr << "waiting for MPIH daemon to start..."
			<< std::endl;

	struct timespec wait_time;
	wait_time.tv_sec = 0;
	/* 200,000,000 nanosecs == 200 millisec */
	wait_time.tv_nsec = 200000000;

	while(true) {
		if (access(opt::pidPath.c_str(), R_OK) == 0)
			break;
		assert(errno == ENOENT);
		nanosleep(&wait_time, NULL);
	}

	/* query daemon for rank and set MPIH_RANK */

	if (opt::verbose)
		std::cerr << "querying daemon for MPI rank..." << std::endl;
	int rank = query_rank();
	if (opt::verbose)
		std::cerr << "our MPI rank is " << rank << std::endl;
	std::ostringstream rankStr;
	rankStr << "MPIH_RANK=" << rank;

	/* query daemon for num ranks and set MPIH_SIZE */

	if (opt::verbose)
		std::cerr << "querying daemon for number of MPI ranks..."
			<< std::endl;
	int size = query_size();
	if (opt::verbose)
		std::cerr << "number of MPI ranks is " << size << std::endl;
	std::ostringstream sizeStr;
	sizeStr << "MPIH_SIZE=" << size;

	/*
	 * run the script specified by the remaining
	 * arguments in argv
	 */

	if (opt::verbose)
		std::cerr << "running user script..." << std::endl;

	std::string path("PATH=");
	if (getenv("PATH") != NULL)
		path.append(getenv("PATH"));

	const unsigned SCRIPT_ENV_SIZE = 7;
	char* envp[SCRIPT_ENV_SIZE];
	envp[0] = strdup(socketStr.c_str());
	envp[1] = strdup(pidStr.c_str());
	envp[2] = strdup(logStr.c_str());
	envp[3] = strdup(rankStr.str().c_str());
	envp[4] = strdup(sizeStr.str().c_str());
	envp[5] = strdup(path.c_str());
	envp[6] = NULL;

	if (optind >= argc) {
		std::cerr << "error: missing arguments" << std::endl;
		std::cerr << RUN_USAGE_MESSAGE;
		exit(EXIT_FAILURE);
	}

	/* fork and run the user's script */
	pid = fork();

	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		if (execve(argv[optind], &argv[optind], envp) < 0) {
			perror("execvpe");
			exit(EXIT_FAILURE);
		}
		/* should never reach this line */
		assert(false);
	}

	/* wait for child process (user script) to complete */
	int status;
	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		exit(EXIT_FAILURE);
	}

	/* free memory allocated for strings */
	for (int i = 0; i < SCRIPT_ENV_SIZE - 1; ++i)
		free(envp[i]);

	/* shut down 'mpih init' daemon */
	finalize();

	if (WIFEXITED(status)) {
		/*
		 * user script exited normally
		 * (exit code may be non-zero)
		 */
		return WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		/*
		 * user script was terminated by
		 * a signal (add 128 to differentiate
		 * signals from exit codes)
		 */
		return WTERMSIG(status) + 128;
	}

	/* should never reach here */
	assert(false);
}

#endif
