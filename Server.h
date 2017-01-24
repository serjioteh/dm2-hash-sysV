#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <algorithm>
#include <set>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/event.h>

#include <errno.h>
#include <err.h>
#include <string.h>

#include <sys/wait.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "HashTable.h"

// MSG_NOSIGNAL does not exists on OS X
#if defined(__APPLE__) || defined(__MACH__)
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif

enum op_type { GET, SET, DEL };

struct Message
{
    long mtype;
    op_type op;
    int key, value, err, from;
};

class Server
{
public:
    Server(uint16_t port)
    {
        start_child();
        init_client_resources(port);
    }

    void start();

    ~Server()
    {
        free_child_resources();
        free_client_resources();
    }
private:
	int  mksock(const char *addr, int port);
	
    void start_child();
    void init_client_resources(uint16_t port);

    void free_child_resources();
    void free_client_resources();

    bool recv_message_from_client(int client_fd);
    void send_message_to_client(int client_fd);

    void disconnect_client(int client_fd);

    char process_message_from_client(Message *msg, int client_fd);
    void process_message_from_child(Message *msg);

    bool recv_message_from_child(Message *msg);
    void send_message_to_child(Message *msg);

public:
    static const size_t MAX_BUF_SIZE = 1024;
    static const long TO_SERVER = 1;
    static const long FROM_SERVER = 2;

private:
    int    binded_socket, kq, socket_sock;
	struct sockaddr_in c; /* client */
	struct kevent ke;
	
    std::set<int> slave_sockets;

    char buffer[MAX_BUF_SIZE+1];

    int msgid, shmid;
	pid_t pid_child;
};

int set_nonblock(int fd);

#endif //SERVER_H
