#ifndef _COMMON_OPTIONS_H_
#define _COMMON_OPTIONS_H_

#include <string>

namespace opt {
	/** -h,--help: show info on command usage */
	int help = 0;
	/**
	 * -s,--socket: Unix socket for communication
	 * between 'init' daemon and client commands
	 * (e.g. 'mpi send').
	 */
	std::string socketPath;
	/** --verbose: verbose output on stderr */
	int verbose = 0;
}

#endif
