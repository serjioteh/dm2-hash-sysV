#include <iostream>
#include <signal.h>

#include "Server.h"

Server *server = NULL;

void handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
		std::cout << "SIGNAL handler called. pid=" << getpid() << std::endl;
        delete server;
        exit(0);
    }
}

int main()
{
    signal(SIGINT, &handler);
    signal(SIGTERM, &handler);

    try
    {
        server = new Server(3100);
        server->start();
        delete server;
    }
    catch (const std::system_error& error)
    {
        if (server)
        {
            delete server;
            server = NULL;
        }
        std::cout << "Error: " << error.code()
                  << " - " << error.code().message() << '\n';
    }
    catch (char const * error)
    {
        if (server)
        {
            delete server;
            server = NULL;
        }
        std::cout << error << std::endl;
    }

    return 0;
}
