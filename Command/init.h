#ifndef _INIT_H_
#define _INIT_H_

#include "Options/CommonOptions.h"
#include "IO/IOUtil.h"
#include "IO/SocketUtil.h"
#include <mpi.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>

static const char INIT_USAGE_MESSAGE[] =
"Usage: mpi init <socket_path>\n"
"\n"
"Description:\n"
"\n"
"   Initialize the current MPI rank and start a daemon\n"
"   that will listen for commands issued from the\n"
"   shell (e.g. 'mpi send').\n"
"\n"
"   Communication between clients (mpi commands) and\n"
"   the daemon occurs over a Unix domain socket located at\n"
"   <socket_path>.  <socket_path> does not need to\n"
"   exist prior to running 'mpi init'. If <socket_path>\n"
"   does exist the daemon will delete the file and recreate\n"
"   it.\n"
"\n"
"Options:\n"
"\n"
"   (none)\n";

namespace mpi {
	int rank;
	int numProc;
}

static const char init_shortopts[] = "hv";

static const struct option init_longopts[] = {
	{ "help",     no_argument, NULL, 'h' },
	{ "verbose",  no_argument, NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

void commandHandler(const char* cmd, int socket_fd, char* dataHead,
	size_t dataHeadLen)
{
	const size_t BUFFER_SIZE = 16384;
	char buffer[BUFFER_SIZE];
	int n = 1;
	while (n > 0)
		n = recv(socket_fd, &buffer, BUFFER_SIZE, 0);
	if (n < 0)
		perror("recv");
}

#define CMD_BUFFER_SIZE 1024
#define SEND_BUFFER_SIZE 16384
#define SEND_CMD "SEND"

enum ConnectionState {
	READING_COMMAND=0,
	RECEIVING_DATA,
	SENDING_DATA,
	CLOSED
};

struct Connection {

	int socket_fd;
	ConnectionState state;

	char* cmdBuffer;
	char* cmdBufferPos;
	char* cmdBufferEnd;

	char* sendBuffer;
	char* sendBufferPos;
	char* sendBufferEnd;

	Connection(int listeningSocket) :
		socket_fd(-1),
		state(CLOSED),
		cmdBuffer(NULL),
		cmdBufferPos(NULL),
		cmdBufferEnd(NULL),
		sendBuffer(NULL),
		sendBufferPos(NULL),
		sendBufferEnd(NULL)
	{
		socket_fd = UnixSocket::accept(listeningSocket);
		setState(READING_COMMAND);
		initCmdBuffer();
	}

	~Connection()
	{
		if (cmdBuffer != NULL)
			free(cmdBuffer);
		if (sendBuffer != NULL)
			free(sendBuffer);
	}

	ConnectionState getState() {
		return state;
	}

	void setState(ConnectionState state) {
		this->state = state;
	}

	void initCmdBuffer()
	{
		if (cmdBuffer == NULL)
			cmdBuffer = (char*)malloc(CMD_BUFFER_SIZE);
		cmdBufferPos = cmdBuffer;
		cmdBufferEnd = cmdBuffer + CMD_BUFFER_SIZE;
	}

	void initSendBuffer()
	{
		if (sendBuffer == NULL)
			sendBuffer = (char*)malloc(SEND_BUFFER_SIZE);
		sendBufferPos = sendBuffer;
		sendBufferEnd = sendBuffer + SEND_BUFFER_SIZE;
	}

	void close()
	{
		if (socket_fd >= 0)
			::close(socket_fd);
		socket_fd = -1;
		setState(CLOSED);
	}

	bool recv()
	{
		assert(getState() == READING_COMMAND);

		// first line from client exceed max length
		if (cmdBufferPos >= cmdBufferEnd) {
			std::cerr << "Client request exceeded max length for "
				<< "command line" << std::endl;
			close();
			return false;
		}

		int n = ::recv(socket_fd, cmdBufferPos,
			cmdBufferEnd - cmdBufferPos, 0);

		// error occurred during recv()
		if (n < 0) {
			perror("recv");
			close();
			return false;
		}

		cmdBufferPos += n;

		// client finished sending data cleanly
		if (n == 0 && cmdBufferPos == cmdBuffer) {
			close();
			return true;
		}

		// find newline that marks end of first line
		char* newLine = (char *)memchr(cmdBuffer,
			'\n', cmdBufferPos - cmdBuffer);

		// newline missing after command
		if (n == 0 && newLine == NULL) {
			std::cerr << "Missing newline after command line" << std::endl;
			close();
			return false;
		}

		// we read a command
		if (newLine != NULL) {
			*newLine = '\0';
			std::cout << "Received command: " << cmdBuffer << std::endl;
			std::string command(cmdBuffer);
			char* remainder = newLine + 1;
			if (command == SEND_CMD) {
				initSendBuffer();
				memcpy(sendBuffer, remainder, cmdBufferPos - remainder);
				cmdBufferPos = cmdBuffer;
				setState(SENDING_DATA);
			} else {
				memmove(cmdBuffer, remainder, cmdBufferPos - remainder);
				cmdBufferPos = cmdBuffer + (cmdBufferPos - remainder);
				setState(READING_COMMAND);
			}
		}

		return true;
	}

};

int serverLoop(const char* socketPath)
{
	std::vector<Connection> connections;

	int s = UnixSocket::listen(socketPath);

	for(;;) {

		std::cout << "Waiting for a connection...\n";

		Connection connection(s);
		while(connection.getState() == READING_COMMAND)
			connection.recv();

	}

	return 0;
}

int cmd_init(int argc, char** argv)
{
	for (int c; (c = getopt_long(argc, argv,
		init_shortopts, init_longopts, NULL)) != -1;) {
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		  case '?':
			die(INIT_USAGE_MESSAGE);
		  case 'h':
			std::cout << INIT_USAGE_MESSAGE;
			return EXIT_SUCCESS;
		  case 'v':
			arg >> opt::verbose;
			break;
		}
		if (optarg != NULL && (!arg.eof() || arg.fail())) {
			std::cerr << "mpi init: invalid option: `-"
				<< (char)c << optarg << "'\n";
			die(INIT_USAGE_MESSAGE);
		}
	}

	// make sure a socket path is given (and nothing else)
	if (argc - optind != 1)
		die(INIT_USAGE_MESSAGE);

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi::numProc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi::rank);

	serverLoop(argv[optind]);

	MPI_Finalize();

	return 0;
}

#endif
