#ifndef _SOCKET_UTIL_H_
#define _SOCKET_UTIL_H_

#include <sys/socket.h>
#include <sys/un.h>

namespace UnixSocket
{
	static inline int open()
	{
		int s;
		if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
			perror("socket");
			exit(EXIT_FAILURE);
		}
		return s;
	}

	static inline int listen(const char* socketPath)
	{
		const int MAX_CONNECTIONS = 5;

		int s = open();

		struct sockaddr_un local;
		local.sun_family = AF_UNIX;
		strcpy(local.sun_path, socketPath);
		unlink(local.sun_path);
		int len = strlen(local.sun_path) + sizeof(local.sun_family);

		if (bind(s, (struct sockaddr *)&local, len) == -1) {
			perror("bind");
			exit(EXIT_FAILURE);
		}

		if (::listen(s, MAX_CONNECTIONS) == -1) {
			perror("listen");
			exit(EXIT_FAILURE);
		}

		return s;
	}

	static inline int connect(const char* socketPath)
	{
		int s = open();

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

	static inline int accept(int socket_fd)
	{
		int s;
		struct sockaddr_un remote;
		socklen_t t = sizeof(remote);
		if ((s = accept(socket_fd, (struct sockaddr *)&remote,
			 &t)) == -1) {
			perror("accept");
			exit(EXIT_FAILURE);
		}
		return s;
	}
}

#endif
