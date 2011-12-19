#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>

#define SEND_LABEL "<< "
#define RECV_LABEL ">> "

int main_server(bool is_udp, const char *server_host, int server_port) {
	int i;
	char buf[1024];

	struct epoll_event ev, evs[10];
	struct sockaddr_in addr;

	int serv_fd = socket(PF_INET, !is_udp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (serv_fd == -1) {
		perror("Failed to create server socket");
		return 11;
	}

	int yes = 1;
	if (setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) {
		perror("Failed to set socket option");
		return 12;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = server_host ? inet_addr(server_host) : htonl(INADDR_ANY);
	addr.sin_port = htons(server_port);

	if (bind(serv_fd, (struct sockaddr*)&addr, sizeof(addr))) {
		close(serv_fd);
		perror("Failed to bind");
		return 13;
	}

	if (!is_udp) {
		listen(serv_fd, 5);

		int selector = epoll_create(10);

		ev.events = EPOLLIN | EPOLLET;
		ev.data.fd = serv_fd;

		if (epoll_ctl(selector, EPOLL_CTL_ADD, serv_fd, &ev)) {
			perror("Failed to register server socket to epoll");
			return 14;
		}

		while (1) {
			int cnt = epoll_wait(selector, evs, sizeof(evs)/sizeof(evs[0]), -1);

			for (i=0;i<cnt;i++) {
				if (evs[i].data.fd == serv_fd) {
					int fd = accept(serv_fd, NULL, NULL);

					ev.events = EPOLLIN | EPOLLET;
					ev.data.fd = fd;

					if (epoll_ctl(selector, EPOLL_CTL_ADD, fd, &ev)) {
						perror("Failed to register client socket to epoll");
						return 15;
					}

					printf("* Here comes a new client.\n");
				} else {
					int buf_read = recv(evs[i].data.fd, buf, sizeof(buf)-1, 0);
					if (buf_read <= 0) {
						close(evs[i].data.fd);
						epoll_ctl(selector, EPOLL_CTL_DEL, evs[i].data.fd, &ev);
					} else {
						buf[buf_read] = '\0';

						send(evs[i].data.fd, buf, strlen(buf), 0);

						printf("* TCP: %s\n", buf);
					}
				}
			}
		}
	} else {
		struct sockaddr_in addr;
		socklen_t addr_len;

		while (1) {
			int buf_read = recvfrom(serv_fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&addr, &addr_len);
			if (buf_read > 0) {
				buf[buf_read] = '\0';

				sendto(serv_fd, buf, buf_read, 0, (struct sockaddr*)&addr, addr_len);

				printf("* UDP: %s\n", buf);
			}
		}
	}

	return 0;
}

int main_client(bool is_udp, const char *server_host, int server_port) {
	int i;
	char ch;
	char line_buf[1024], *line_buf_pos = line_buf;
	char buf[1024];

	struct epoll_event ev, evs[10];
	struct sockaddr_in addr;
	struct hostent *hent;

	if (!(hent = gethostbyname(server_host))) {
		perror("Failed to resolve hostname");
		return 11;
	}

	int sock_fd = socket(PF_INET, !is_udp ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (sock_fd == -1) {
		perror("Failed to create client socket");
		return 12;
	}

	int selector = epoll_create(10);

	int flags = fcntl(STDIN_FILENO, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL, flags);

	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = STDIN_FILENO;

	if (epoll_ctl(selector, EPOLL_CTL_ADD, STDIN_FILENO, &ev)) {
		perror("Failed to register stdin to epoll");
		return 13;
	}

	addr.sin_family = AF_INET;
	memcpy(&addr.sin_addr, hent->h_addr_list[0], hent->h_length);
	addr.sin_port = htons(server_port);

	if (!is_udp) {
		if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr))) {
			perror("Failed to connect to server");
			return 14;
		}
	} else {
		struct sockaddr_in addr;

		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(0);

		if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr))) {
			perror("Failed to bind client socket");
			return 15;
		}
	}

	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = sock_fd;

	if (epoll_ctl(selector, EPOLL_CTL_ADD, sock_fd, &ev)) {
		perror("Failed to register client socket to epoll");
		return 16;
	}

	printf("%s", SEND_LABEL);
	fflush(stdout);

	bool cont = true;
	while (cont) {
		int cnt = epoll_wait(selector, evs, sizeof(evs)/sizeof(evs[0]), -1);

		for (i=0;i<cnt;i++) {
			if (evs[i].data.fd == STDIN_FILENO) {
				while (1) {
					if (read(evs[i].data.fd, &ch, 1) != 1) break;

					if (ch != '\n' && line_buf_pos != line_buf+sizeof(line_buf)/sizeof(line_buf[0])-1) {
						*line_buf_pos++ = ch;
					} else if (ch == '\n') {
						*line_buf_pos = '\0';
						line_buf_pos = line_buf;

						if (*line_buf) {
							if (!is_udp) send(sock_fd, line_buf, strlen(line_buf), 0);
							else sendto(sock_fd, line_buf, strlen(line_buf), 0, (struct sockaddr*)&addr, sizeof(addr));
						}

						printf("%s", SEND_LABEL);
						fflush(stdout);
					}
				}
			} else {
				int buf_read = recv(evs[i].data.fd, buf, sizeof(buf)-1, 0);
				if (buf_read <= 0) {
					fprintf(stderr, "Connection closed.\n");
					cont = false;
					break;
				} else {
					buf[buf_read] = '\0';

					printf("\r%s%s\n%s", RECV_LABEL, buf, SEND_LABEL);
					fflush(stdout);
				}
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	int opt;
	const char *server_host = NULL;
	int server_port = L'엨';

	enum {
		SERVICE_UNKNOWN = -1,
		SERVICE_SERVER = 0,
		SERVICE_CLIENT = 1,
	} service_type = SERVICE_UNKNOWN;

	enum {
		INTERFACE_UNKNOWN = -1,
		INTERFACE_TCP = 0,
		INTERFACE_UDP = 1,
	} interface_type = INTERFACE_UNKNOWN;

	while ((opt = getopt(argc, argv, "sctuh:p:")) != -1) {
		switch (opt) {
			case 's':
				service_type = SERVICE_SERVER;
				break;

			case 'c':
				service_type = SERVICE_CLIENT;
				break;

			case 't':
				interface_type = INTERFACE_TCP;
				break;

			case 'u':
				interface_type = INTERFACE_UDP;
				break;

			case 'h':
				server_host = optarg;
				break;

			case 'p':
				server_port = strtol(optarg, NULL, 10);
				break;

			case '?':
				if (optopt == 'h' || optopt == 'p') {
					fprintf(stderr, "-%c requires an argument.\n", optopt);
					return 1;
				}

				break;
		}
	}

	if (service_type == SERVICE_UNKNOWN) {
		fprintf(stderr, "Choose service type.\n");
		return 2;
	}

	if (interface_type == INTERFACE_UNKNOWN) {
		fprintf(stderr, "Choose interface type.\n");
		return 3;
	}

	if (service_type == SERVICE_CLIENT && !server_host) server_host = "localhost";

	int (*ent_pnts[])(bool, const char*, int) = {main_server, main_client};
	return ent_pnts[service_type](interface_type, server_host, server_port);
}
