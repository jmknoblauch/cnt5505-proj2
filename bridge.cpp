/*----------------------------------------------------------------*/
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
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
#include <sys/select.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include "ip.h"
/*----------------------------------------------------------------*/

#define SERVER_PORT 5100
#define DOMAIN_NAME_SIZE 256
#define MESSAGE_SIZE 200

using namespace std;

struct client_t
{
   int port;
   char name[DOMAIN_NAME_SIZE];
};

struct bridge_table_entry
{
    int sockfd;
    MacAddr macaddr;
    bridge_table_entry(int a, MacAddr m):sockfd(a)
    {
        strncpy(macaddr, m, 18);
    }
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
    
    if (argc != 3)
    {
        cout << "Usage: bridge <lan-nam> <num-ports>" << endl;
        exit(0);
    }

    ofstream port_file, addr_file;
    int servfd, maxfd, num_ports = atoi(argv[2]) + 4;
    struct sockaddr_in serverAddr, newaddr;
    socklen_t length;
    fd_set readset;
    client_t clients[num_ports];
    char domainName[DOMAIN_NAME_SIZE], message[MESSAGE_SIZE];
    string port_fname, addr_fname;
    struct hostent *host;
    EtherPkt ether_pkt;
    IP_PKT IP_pkt;
    ARP_PKT ARP_pkt;
    char buffer[SHRT_MAX];
    vector<bridge_table_entry> bridge_table;

    // This is used so that ctrl+C can be properly handled
    signal(SIGINT, intHandler);

    // Initialize the message buffer and client arrays to 0
    memset (clients, 0, num_ports*sizeof(client_t));
    memset (message, 0, MESSAGE_SIZE*sizeof(char));

    // Open a new socket
    servfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // Variable to keep track of the largest file descriptor
    maxfd = servfd + 1;

    serverAddr.sin_port = 0;
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
    // Not needed?
    gethostname(domainName, DOMAIN_NAME_SIZE);
    host = gethostbyname(domainName);
    strcpy(clients[servfd].name, host->h_name);

    // create the LAN files for the port and IP address
    port_fname = string(".") + argv[1] + ".port";
    addr_fname = string(".") + argv[1] + ".addr";
    port_file.open(port_fname.c_str());
    addr_file.open(addr_fname.c_str());
    port_file << clients[servfd].port << endl;
    addr_file << inet_ntoa((*(struct in_addr *) host->h_addr)) << endl; 
    port_file.close();
    addr_file.close();

    cout << "admin: started server on '" << host->h_name << "' at '" 
         << clients[servfd].port << "'\n"
         << "admin: IP address - " 
         << inet_ntoa((*(struct in_addr *) host->h_addr)) << endl
         << "(You can specify the port by using chatserver <port number>)\n";
  
    /* listen to the socket.
     * two cases:
     * 1. connection open/close request from stations/routers
     * 2. regular data ether_pkts
     */

    while(noInterupt)
    {
        // Reset the read status of all sockets at the beginning of each loop
        FD_ZERO(&readset);

        // Set the read status for the server and active client sockets
        FD_SET(servfd, &readset);
        for(int i = 0; i < num_ports; i++)
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

                if (newfd < num_ports)
                {
                    send(newfd, "accept", 7, 0);

                    // Get the client's port and host name
                    clients[newfd].port = ntohs(newaddr.sin_port);
                    host = gethostbyaddr((const void*)&newaddr.sin_addr,4,AF_INET);
                    //strcpy(clients[newfd].name, host->h_name);
                    if (newfd >= maxfd)
                        maxfd = newfd + 1;
                    cout << "admin: connect from '" << clients[newfd].port << "'\n";
                }
                else
                {
                    send(newfd, "reject", 7, 0);
                    close(newfd);
                }
            }
            // If activity from one of the clients, retrieve its message
            else
            {
                for(int i = 3; i < num_ports; ++i)
                {
                    if(FD_ISSET(i, &readset))
                    {
                        // Get ether_pkt and check for disconnection
                        if(recv(i, &ether_pkt, sizeof(EtherPkt), 0) == 0) 
                        {
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
                        else if (ether_pkt.src[0])
                        {
                            //cout << "(" << clients[i].port << "): "
                            //     << ether_pkt.dat << " size " << ether_pkt.size << endl;
                            //cout << "ether_pkt.dst = " << ether_pkt.dst << "\n";
                            
                            if (ether_pkt.type == TYPE_IP_PKT)
                                recv(i, &IP_pkt, sizeof(IP_PKT), 0);
                            else if (ether_pkt.type == TYPE_ARP_PKT)
                                recv(i, &ARP_pkt, sizeof(ARP_PKT), 0);

                            bool found = 0;

                            // Check if src mac is already in bridge table
                            for (int j = 0; j < bridge_table.size(); ++j)
                            {
                                if (!strncmp(ether_pkt.src, bridge_table[j].macaddr, 18))
                                {
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                //bridge_table_entry new_entry;
                                //strncpy(new_entry.macaddr, ether_pkt.src, 18);
                                //new_entry.sockfd = i;
                                bridge_table.push_back(bridge_table_entry(i, ether_pkt.src));
                                cout << bridge_table[bridge_table.size() - 1].sockfd << " -> " 
                                     << bridge_table[bridge_table.size() - 1].macaddr << endl;
                            }
                            
                            found = 0;
                            // Check to see if we should search for dst mac in bridge table
                            if (ether_pkt.dst[0] != 'x')
                            {

                                cout << "Want to send to " << ether_pkt.dst << endl;

                                for (int j = 0; j < bridge_table.size(); ++j)
                                {
                                    if (!strncmp(ether_pkt.dst, bridge_table[j].macaddr, 18))
                                    {
                                        // MAC found in table
                                        cout << "Sending ether_pkt to " << bridge_table[j].sockfd << endl;
                                        send(bridge_table[j].sockfd, &ether_pkt, sizeof(EtherPkt), 0);
                                        if (ether_pkt.type == TYPE_IP_PKT)
                                        {
                                            cout << "And ip_pkt\n";
                                            send(bridge_table[j].sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                                        }
                                        else if (ether_pkt.type == TYPE_ARP_PKT)
                                        {
                                            cout << "And arp_pkt\n";

                                            send(bridge_table[j].sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
                                        }
                                        found = 1;
                                        break;
                                    }
                                }
                            }
                            if (!found)
                            {
                                cout << "Broadcasting ether_pkt\n";
                                for(int j = 0; j < num_ports; ++j)
                                {
                                    if(clients[j].port && j != i &&  j != servfd)
                                    {
                                        send(j, &ether_pkt, sizeof(EtherPkt), 0);
                                        if (ether_pkt.type == TYPE_IP_PKT)
                                        {
                                            cout << "And ip_pkt\n";
                                            send(j, &IP_pkt, sizeof(IP_PKT), 0);
                                        }
                                        else if (ether_pkt.type == TYPE_ARP_PKT)
                                        {
                                            cout << "And arp_pkt\n";
                                            send(j, &ARP_pkt, sizeof(ARP_PKT), 0);
                                        }
                                    }
                                }
                            }
                        }

                        // Clear the message buffer afterward
                        memset(&ether_pkt, 0, sizeof(EtherPkt));
                        memset(&ARP_pkt, 0, sizeof(ARP_PKT));
                        memset(&IP_pkt, 0, sizeof(IP_PKT));

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
