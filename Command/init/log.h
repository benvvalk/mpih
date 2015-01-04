#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>

namespace opt {
	static std::string logPath;
}

static FILE* g_log = NULL;

static inline void init_log()
{
	// make sure file is not already open
	assert(g_log == NULL);

	if (opt::logPath.empty())
		opt::logPath = "/dev/null";

	if (opt::logPath == "-") {
		g_log = stdout;
		return;
	}

	g_log = fopen(opt::logPath.c_str(), "w");
	if (g_log == NULL) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}

	if (setvbuf(g_log, NULL, _IOLBF, 0)) {
		perror("setvbuf");
		exit(EXIT_FAILURE);
	}
}

static inline void close_log()
{
	assert(g_log != NULL);
	if (fclose(g_log) < 0)
		perror("fclose");
}

#endif
