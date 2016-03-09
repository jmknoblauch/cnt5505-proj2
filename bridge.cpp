/*----------------------------------------------------------------*/
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
/*----------------------------------------------------------------*/

#define SERVER_PORT 5100
#define DOMAIN_NAME_SIZE 256
#define MESSAGE_SIZE 200
#define MAX_CLIENTS 256

using namespace std;

struct client_t
{
   int port;
   char name[DOMAIN_NAME_SIZE];
};

static volatile int noInterupt = 1;

void intHandler(int dummy) 
{
    noInterupt = 0;
}

/* bridge : recvs pkts and relays them */
/* usage: bridge lan-name max-port */
int main (int argc, char *argv[]) 
{
    /* create the symbolic links to its address and port number
     * so that others (stations/routers) can connect to it
     */
    
    int servfd, servport, result, maxfd;
    struct sockaddr_in serverAddr, newaddr;
    socklen_t length;
    fd_set readset;
    client_t clients[MAX_CLIENTS];
    char domainName[DOMAIN_NAME_SIZE], message[MESSAGE_SIZE];
    struct hostent *host;

    // This is used so that ctrl+C can be properly handled
    signal(SIGINT, intHandler);

    // Initialize the message buffer and client arrays to 0
    memset (clients, 0, MAX_CLIENTS*sizeof(client_t));
    memset (message, 0, MESSAGE_SIZE*sizeof(char));

    // Open a new socket
    servfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // Variable to keep track of the largest file descriptor
    maxfd = servfd + 1;

    // Assign the port from the command line argument if provided
    if (argc == 2)
        servport = htons(atoi(argv[1]));
    else
        servport = 0;
    serverAddr.sin_port = servport;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_family = AF_INET;

    // Bind the socket to a free port (by specifying a port of 0)
    length = sizeof(serverAddr);
    bind (servfd, (struct sockaddr *) &serverAddr, length);
    listen (servfd, maxfd);

    // Get the sockaddr that was created to find the assigned port
    getsockname(servfd, (struct sockaddr *) &serverAddr, &length);
    clients[servfd].port = ntohs(serverAddr.sin_port);

    // Get the server host name
    gethostname(domainName, DOMAIN_NAME_SIZE);
    host = gethostbyname(domainName);
    strcpy(clients[servfd].name, host->h_name);

    cout << "admin: started server on '" << host->h_name << "' at '" 
         << clients[servfd].port << "'\n"
         << "admin: IP address - " 
         << inet_ntoa((*(struct in_addr *) host->h_addr)) << endl
         << "(You can specify the port by using chatserver <port number>)\n";
  
    /* listen to the socket.
     * two cases:
     * 1. connection open/close request from stations/routers
     * 2. regular data packets
     */

    while(noInterupt)
    {
        // Reset the read status of all sockets at the beginning of each loop
        FD_ZERO(&readset);

        // Set the read status for the server and active client sockets
        FD_SET(servfd, &readset);
        for(int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].port != 0)
                FD_SET(i, &readset);

        // Block until activity on one of the sockets
        if(select(maxfd, &readset, NULL, NULL, NULL) > 0)
        {
            // If activity on the server, there is a new client
            if(FD_ISSET(servfd, &readset))
            {
                int newfd;
                length = sizeof(newaddr);
                newfd = accept(servfd, (struct sockaddr *) &newaddr, &length);

                // Get the client's port and host name
                clients[newfd].port = ntohs(newaddr.sin_port);
                host = gethostbyaddr((const void*)&newaddr.sin_addr, 4, AF_INET);
                strcpy(clients[newfd].name, host->h_name);

                if (newfd >= maxfd)
                    maxfd = newfd + 1;
                cout << "admin: connect from '" << clients[newfd].name
                     << "' at '" << clients[newfd].port << "'\n";
            }
            // If activity from one of the clients, retrieve its message
            else
            {
                for(int i = 0; i < MAX_CLIENTS; ++i)
                {
                    if(FD_ISSET(i, &readset))
                    {
                        result = recv(i, message, MESSAGE_SIZE, 0);
                        // In case of a disconnection
                        if(result == 0){
                            close(i);
                            if(i == (maxfd - 1))
                                maxfd = maxfd - 1;

                            cout << "admin: disconnect from '" 
                                 << clients[i].name << "(" 
                                 << clients[i].port << ")'\n";
                            // Zero port is used to show socket is inactive
                            clients[i].port = 0;
                        }
                        // If message recieved, echo to all other clients
                        else
                        {
                            cout << clients[i].name << "(" 
                                 << clients[i].port << "): " << message;
                            for(int j = 0; j < MAX_CLIENTS; ++j)
                            {
                                if(clients[j].port != 0 && j != i && j != servfd)
                                    send(j, message, strlen(message), 0);
                            }
                        }
                        // Clear the message buffer afterward
                        memset(message, 0, MESSAGE_SIZE);

                    }
                }
            }
        }
    }

    cout << "\nShutting down.\n";

    close(servfd);
    return 0;

}
/*----------------------------------------------------------------*/