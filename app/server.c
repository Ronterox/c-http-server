#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define FILE_LIMIT 100

struct client_args {
	int client_fd;
	char (*files)[256];
	int file_count;
};

void *handle_client(void *args) {
	struct client_args *client_args = (struct client_args *)args;
	int client_fd = client_args->client_fd;

	char input_buffer[1024];
	if (read(client_fd, input_buffer, sizeof(input_buffer)) < 0) {
		printf("Read failed: %s \n", strerror(errno));
		return NULL;
	}

	printf("Received request:\n%s\n", input_buffer);
	strtok(input_buffer, " ");
	char *path = strtok(NULL, " ");
	if (!path) {
		const char *response = "HTTP/1.1 400 Bad Request\r\n"
							   "Content-Length: 0"
							   "\r\n\r\n";
		send(client_fd, response, strlen(response), 0);
		return NULL;
	}

	const char ok[] = "HTTP/1.1 200 OK\r\n";
	if (strcmp(path, "/") == 0) {
		const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 0";
		send(client_fd, response, strlen(response), 0);

	} else if (strncmp(path, "/echo/", 6) == 0) {
		const char *body = strlen(path) > 6 ? path + 6 : "";

		char response[1024];
		int full_length = sprintf(response,
								  "%sContent-Type: text/plain\r\n"
								  "Content-Length: %ld\r\n\r\n%s",
								  ok, strlen(body), body);

		send(client_fd, response, full_length, 0);

	} else if (strcmp(path, "/user-agent") == 0) {
		char *line;
		do {
			line = strtok(NULL, "\r\n");
			if (strncmp(line, "User-Agent: ", 12) == 0) {
				char *user_agent = line + 12;
				char response[1024];
				int full_length = sprintf(response,
										  "%sContent-Type: text/plain\r\n"
										  "Content-Length: %ld\r\n\r\n%s",
										  ok, strlen(user_agent), user_agent);
				send(client_fd, response, full_length, 0);
				break;
			}
		} while (line);

	} else if (strncmp(path, "/files/", 7) == 0) {
		for (int i = 0; i < client_args->file_count; i++) {
			char *filename = client_args->files[i];
			printf("Filename: %s, Path: %s\n", filename, path + 7);
			if (strcmp(path + 7, filename) == 0) {
				FILE *file = fopen(filename, "r");
				if (!file) {
					printf("File not found: %s\n", strerror(errno));
					return NULL;
				}

				fseek(file, 0, SEEK_END);
				long file_size = ftell(file);

				char *file_contents = malloc(file_size);
				fseek(file, 0, SEEK_SET);
				fread(file_contents, 1, file_size, file);

				fclose(file);

				printf("File size: %ld\n", file_size);
				char response[1024];
				int full_length =
					sprintf(response,
							"%sContent-Type: application/octet-stream\r\n"
							"Content-Length: %ld\r\n\r\n%s",
							ok, file_size, file_contents);
				send(client_fd, response, full_length, 0);
				free(file_contents);
			}
		}

	} else {
		const char *response = "HTTP/1.1 404 Not Found\r\n"
							   "Content-Length: 0";
		send(client_fd, response, strlen(response), 0);
	}

	send(client_fd, "\r\n\r\n", 4, 0);
	return NULL;
}

int setup_server(int *server_fd) {
	*server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (*server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting
	// REUSE_PORT ensures that we don't run into 'Address already in use'
	// errors
	const int reuse = 1;
	if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse,
				   sizeof(reuse)) < 0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(*server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) !=
		0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	const int connection_backlog = 5;
	printf("Listening on http://localhost:4221\n");
	if (listen(*server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	return 0;
}

char *set_directory(int *argc, char *argv[]) {
	int opt;
	char *directory = ".";
	const static struct option long_options[] = {
		{"directory", required_argument, 0, 'd'}};

	while ((opt = getopt_long(*argc, argv, "d:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			directory = optarg;
			break;
		default:
			printf("Usage: %s --directory <path>\n", argv[0]);
			return NULL;
		}
	}
	return directory;
}

int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be
	// visible when running tests.
	printf("Logs from your program will appear here!\n");

	char *directory = set_directory(&argc, argv);
	if (!directory) {
		return 1;
	}
	printf("Directory: %s\n", directory);

	DIR *dir = opendir(directory);
	if (!dir) {
		printf("Directory not found: %s\n", strerror(errno));
		return 1;
	}

	struct dirent *entry;
	char files[FILE_LIMIT][256];
	int file_count = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG) {
			printf("File: %s\n", entry->d_name);
			strcpy(files[file_count], entry->d_name);
			file_count++;
		}
	}
	printf("File count: %d\n", file_count);
	closedir(dir);

	int server_fd;
	if (setup_server(&server_fd) != 0) {
		return 1;
	}

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	while (1) {
		printf("Waiting for a client to connect...\n");
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr,
							   &client_addr_len);
		if (client_fd < 0) {
			printf("Accept failed: %s \n", strerror(errno));
			return 1;
		}
		printf("Client %d connected\n", client_fd);

		pthread_t thread;
		struct client_args args = {client_fd, files, file_count};
		if (pthread_create(&thread, NULL, &handle_client, &args) != 0) {
			printf("Thread creation failed: %s \n", strerror(errno));
			return 1;
		}
		pthread_detach(thread);
	}

	close(server_fd);
	for (int i = 0; i < file_count; i++) {
		free(files[i]);
	}

	return 0;
}
