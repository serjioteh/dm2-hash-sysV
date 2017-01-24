#include "Server.h"

int Server::mksock(const char *addr, int port)
{
	int i;
	struct sockaddr_in serv;

	socket_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_sock == -1)
        throw std::system_error(errno, std::system_category());

	i = 1;
	if (setsockopt(socket_sock, SOL_SOCKET, SO_REUSEADDR, (void *)&i, (socklen_t)sizeof(i)) == -1)
		warn("setsockopt");

	memset(&serv, 0, sizeof(struct sockaddr_in));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(port);
	serv.sin_addr.s_addr = inet_addr(addr);

	i = bind(socket_sock, (struct sockaddr *)&serv, (socklen_t)sizeof(serv));
	if (i == -1)
        throw std::system_error(errno, std::system_category());

	set_nonblock(socket_sock);

	i = listen(socket_sock, SOMAXCONN);
	if (i == -1)
        throw std::system_error(errno, std::system_category());

	return(socket_sock);
}

void Server::start_child()
{
    key_t key = ftok("./sysVipc", 'b');
    msgid = msgget(key, 0666 | IPC_CREAT);
    shmid = shmget(key, HashTable::TOTAL_MEM_SIZE, 0666 | IPC_CREAT);

    void *shmaddr = shmat(shmid, NULL, 0);
	if ((uint64_t)shmaddr == -1)
		throw std::system_error(errno, std::system_category());
	
    HashTable::shared_memory_set(shmaddr);
    shmdt(shmaddr);

    if ((pid_child = fork()) == 0)
    {
		Message msg;
	    shmaddr = shmat(shmid, NULL, 0);
	    HashTable hash_table(shmaddr);

	    while (msgrcv(msgid, (struct msgbuf*) &msg, sizeof(msg), FROM_SERVER, 0) > 0)
	    {
			std::cout << "DEBUG: msg received by child" << std::endl;
			
	        msg.mtype = TO_SERVER;
	        try
	        {
	            switch (msg.op)
	            {
	                case SET:
	                    hash_table.set(msg.key, msg.value);
	                    break;

	                case GET:
	                    msg.value = hash_table.get(msg.key);
	                    break;

	                case DEL:
	                    hash_table.del(msg.key);
	                    break;

	                default: ;
	            }

	            msg.err = 0;
	        }
	        catch (HashTableError err)
	        {
	            switch (err.getType())
	            {
	                case HashTableErrorType::NoKey:     msg.err = 1; break;
	                case HashTableErrorType::NoMemory:  msg.err = 2; break;
	            }
	        }

			std::cout << "DEBUG: msg sent by child" << std::endl;
	        msgsnd(msgid, (struct msgbuf*) &msg, sizeof(msg), 0);
	    }
    }
	else
	{
		std::cout << "DEBUG: child process was forked. pid=" << pid_child << std::endl;			
	}    
}


void Server::init_client_resources(uint16_t port)
{
	/* get a listening socket */
	const char *addr = "127.0.0.1";
	binded_socket = mksock(addr, port);

	/* get our kqueue descriptor */
	kq = kqueue();
	if (kq == -1)
        throw std::system_error(errno, std::system_category());

	memset(&ke, 0, sizeof(struct kevent));

	/* fill out the kevent struct */
	EV_SET(&ke, binded_socket, EVFILT_READ, EV_ADD, 0, 5, NULL);

	/* set the event */
	int i = kevent(kq, &ke, 1, NULL, 0, NULL);
	if (i == -1)
        throw std::system_error(errno, std::system_category());
}

void Server::free_child_resources()
{
    std::cout << "DEBUG: wait(&status) returned: " << wait(NULL) << std::endl;

    shmctl(shmid, IPC_RMID, 0);
    msgctl(msgid, IPC_RMID, 0);
}

void Server::free_client_resources()
{
    for (auto client: slave_sockets)
        disconnect_client(client);
	slave_sockets.clear();

	EV_SET(&ke, binded_socket, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	int i = kevent(kq, &ke, 1, 0, 0, NULL);
	if (i == -1)
		throw std::system_error(errno, std::system_category());

    close(binded_socket);
    shutdown(socket_sock, SHUT_RDWR);
    close(socket_sock);
}

void Server::start()
{
    Message msg;
	struct timespec timespc;
	timespc.tv_sec = 0;     /* seconds */
	timespc.tv_nsec = 1e8;  /* nanoseconds */

    while (1) {
		memset(&ke, 0, sizeof(ke));
		
		/* receive an event, a blocking call as timeout is NULL */
//		int i = kevent(kq, NULL, 0, &ke, 1, NULL);

		/* receive an event, a non blocking version */
		int i = kevent(kq, NULL, 0, &ke, 1, &timespc);
		
		if (i == -1)
	        throw std::system_error(errno, std::system_category());

        if (recv_message_from_child(&msg))
        {
			std::cout << "DEBUG: msg received by server" << std::endl;
			
            process_message_from_child(&msg);
            send_message_to_client(msg.from);
        }

		if (i == 0)
			continue;

		/*
		 * since we only have one kevent in the eventlist, we're only
		 * going to get one event at a time
		 */

		if (ke.ident == binded_socket)
		{
			/* server socket, theres a client to accept */
			socklen_t len = (socklen_t)sizeof(c);
			int SlaveSocket = accept(binded_socket, (struct sockaddr *)&c, &len);
			if (SlaveSocket == -1)
		        throw std::system_error(errno, std::system_category());

			set_nonblock(SlaveSocket);

			memset(&ke, 0, sizeof(ke));
			EV_SET(&ke, SlaveSocket, EVFILT_READ, EV_ADD | EV_EOF, 0, 0, NULL);
			int i = kevent(kq, &ke, 1, NULL, 0, NULL);
			if (i == -1)
		        throw std::system_error(errno, std::system_category());

		    slave_sockets.insert(SlaveSocket);
		    std::cout << "DEBUG: " << "fd " << SlaveSocket  << " connected" << std::endl;
		    send(SlaveSocket, "Welcome!\n", 9, MSG_NOSIGNAL);
			
		}
        else
		{   
			// client event
		    int client_fd = ke.ident;

		    if (recv_message_from_client(client_fd))
		    {
		        Message msg;
		        int res = process_message_from_client(&msg, client_fd);

		        if (res > 0)
				{
					// ok
		            send_message_to_child(&msg);
				}
		        else if (res < 0)
				{
					// error message
		            send_message_to_client(client_fd);
				}

		        if (ke.flags & EV_EOF) // it doesn't work 
				{
					std::cout << "EV_EOF" << std::endl;
		            disconnect_client(client_fd);
				}
		    }
		}
    }
}


bool Server::recv_message_from_client(int client_fd)
{
	memset(buffer, 0, sizeof(buffer));
	ssize_t recv_size = read(client_fd, buffer, MAX_BUF_SIZE);

    if (recv_size <= 0)
    {
		/* EOF from a client */
        disconnect_client(client_fd);
        return false;
    }
    return true;
}

void Server::disconnect_client(int client_fd)
{
	EV_SET(&ke, client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	int i = kevent(kq, &ke, 1, 0, 0, NULL);
	if (i == -1)
        throw std::system_error(errno, std::system_category());

    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);

    std::cout << "DEBUG: " << "fd " << client_fd  << " disconnected" << std::endl;
}

void Server::send_message_to_client(int client_fd)
{
    send(client_fd, buffer, strlen(buffer) + 1, MSG_NOSIGNAL);
}

char Server::process_message_from_client(Message *msg, int client_fd)
{
    char cmd[32];
    int key, value, n_args;

    if ((n_args = sscanf(buffer, "%s%d%d", cmd, &key, &value)) <= 0)
        return 0;

    std::cout << "[CMD from " << client_fd << "]:";

    msg->key   = key;
    msg->value = value;
    msg->from  = client_fd;
    msg->mtype = FROM_SERVER;

    if (strcmp(cmd, "set") == 0 && n_args == 3)
    {
        msg->op = SET;
    }
    else if (strcmp(cmd, "get") == 0 && n_args == 2)
    {
        msg->op = GET;
    }
    else if (strcmp(cmd, "del") == 0 && n_args == 2)
    {
        msg->op = DEL;
    }
    else
    {
        std::cout << buffer;

        std::cout << "DEBUG: Command from " << msg->from << " is FAILED: "
                  << "Wrong command or wrong number of params" << std::endl;

        sprintf(buffer, "> FAIL: Wrong command or wrong number of params\n");
        return -1;
    }

    if (n_args >= 1)
    {
        std::cout << ' ' << cmd;
        if (n_args >= 2)
        {
            std::cout << ' ' << key;
            if (n_args >= 3)
                std::cout << ' ' << value;
        }
    }
    std::cout << std::endl;
    return 1;
}

void Server::process_message_from_child(Message *msg)
{
    switch (msg->err)
    {
        case 0:
            std::cout << "DEBUG: Command from " << msg->from << " is DONE" << std::endl;
			if (msg->op == GET)
	            sprintf(buffer, "RESULT: (%d , %d)\n", msg->key, msg->value);
			else
	            sprintf(buffer, "DONE.\n");
            break;

        case 1:
            std::cout << "DEBUG: Command from " << msg->from << " is FAILED: "
                      << "There is no key " << msg->key << std::endl;
            sprintf(buffer, "FAIL: There is no key %d\n", msg->key);
            break;

        case 2:
            std::cout << "DEBUG: Command from " << msg->from << " is FAILED: "
                      << "There is no more free memory left" << std::endl;
            sprintf(buffer, "FAIL: There is no more free memory left\n");
            break;

        default: ;
    }
}

void Server::send_message_to_child(Message *msg)
{
	std::cout << "DEBUG: msg sent by server" << std::endl;
		
    msgsnd(msgid, (struct msgbuf*) msg, sizeof(Message), 0);
}

bool Server::recv_message_from_child(Message *msg)
{
    return (msgrcv(msgid, (struct msgbuf*) msg, sizeof(Message), TO_SERVER, IPC_NOWAIT) > 0);
}

int set_nonblock(int fd)
{
    int flags;
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}