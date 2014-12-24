#ifndef _SOCKET_UTIL_H_
#define _SOCKET_UTIL_H_

#include <sys/socket.h>
#include <sys/un.h>

static inline int openUnixSocket()
{
	int s;
	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	return s;
}

static inline int
listenOnUnixSocket(const char* socketPath)
{
	const int MAX_CONNECTIONS = 5;

	int s = openUnixSocket();

	struct sockaddr_un local;
	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, socketPath);
	unlink(local.sun_path);
	int len = strlen(local.sun_path) + sizeof(local.sun_family);

	if (bind(s, (struct sockaddr *)&local, len) == -1) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(s, MAX_CONNECTIONS) == -1) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	return s;
}

static inline int connectToUnixSocket(const char* socketPath)
{
	int s = openUnixSocket();

	struct sockaddr_un remote;
	remote.sun_family = AF_UNIX;
	strcpy(remote.sun_path, socketPath);
	int len = strlen(remote.sun_path) + sizeof(remote.sun_family);

	if (connect(s, (struct sockaddr *)&remote, len) == -1) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	return s;
}

#endif
