#ifndef _COMMANDS_H_
#define _COMMANDS_H_

#include "Command/finalize.h"
#include "Command/help.h"
#include "Command/init.h"
#include "Command/rank.h"
#include "Command/recv.h"
#include "Command/run.h"
#include "Command/send.h"
#include "Command/size.h"
#include "Command/version.h"
#include "Macro/Array.h"
#include "IO/IOUtil.h"
#include <iostream>
#include <cstring>

struct cmd_struct {
	const char* name;
	int (*func)(int, char**);
};

static struct cmd_struct cmd_map[] = {
	{ "finalize", &cmd_finalize },
	{ "help", &cmd_help },
	{ "--help", &cmd_help },
	{ "-h", &cmd_help },
	{ "init", &cmd_init },
	{ "rank", &cmd_rank },
	{ "recv", &cmd_recv },
	{ "run", &cmd_run },
	{ "send", &cmd_send },
	{ "size", &cmd_size },
	{ "--version", &cmd_version },
	{ "version", &cmd_version }
};

int invoke_cmd(const char* cmd, int argc, char** argv)
{
	for (unsigned i = 0; i < ARRAY_SIZE(cmd_map); ++i) {
		if (!strcmp(cmd_map[i].name, cmd)) {
			return cmd_map[i].func(argc, argv);
		}
	}
	die(USAGE_MESSAGE);
}

#endif
