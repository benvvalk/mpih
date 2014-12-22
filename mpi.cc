#include "Command/commands.h"
#include <string>

using namespace std;

int main(int argc, char** argv)
{
	if (argc < 2)
		die(USAGE_MESSAGE);

	string command(argv[optind++]);
	return invoke_cmd(command.c_str(), argc, argv);

	die(USAGE_MESSAGE);
}
