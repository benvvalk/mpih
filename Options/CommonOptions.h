#ifndef _COMMON_OPTIONS_H_
#define _COMMON_OPTIONS_H_

#include <string>

namespace opt {
	/** -f,--fifo: command pipe to mpi daemon */
	std::string fifoPath;
	/** -h,--help: show info on command usage */
	int help = 0;
	/** --verbose: verbose output on stderr */
	int verbose = 0;
}

#endif
