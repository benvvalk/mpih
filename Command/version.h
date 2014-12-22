#ifndef _VERSION_H_
#define _VERSION_H_

#include "config.h"
#include <iostream>

static const char VERSION_MESSAGE[] =
"mpiglue " MPIGLUE_VERSION "\n"
"Written by Ben Vandervalk.\n";

int cmd_version(int argc, char** argv)
{
	std::cout << VERSION_MESSAGE;
	return EXIT_SUCCESS;
}

#endif
