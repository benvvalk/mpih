#ifndef _COMMON_OPTIONS_H_
#define _COMMON_OPTIONS_H_

#include <string>

namespace opt {
	/** -h,--help: show info on command usage */
	extern int help;
	/**
	 * -s,--socket: Unix socket for communication
	 * between 'init' daemon and client commands
	 * (e.g. 'mpi send').
	 */
	extern std::string socketPath;
	/** --verbose: verbose output on stderr */
	extern int verbose;
}

#endif
