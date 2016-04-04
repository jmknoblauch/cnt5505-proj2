/* Course: CNT5505 - Data and Computer Communications
   Semester: Spring 2016
   Name: Jacob Knoblauch
 */

#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/select.h>
#include <fstream>
#include <vector>
#include "ip.h"

#define MESSAGE_SIZE 200

using namespace std;

unsigned long stoIP (const char * a);
string IPtos (unsigned long IP);

int main(int argc, char** argv)
{
    char message[MESSAGE_SIZE];
    int sockfd, maxfd = 0;
    fd_set readset;
    struct hostent *host;
    struct sockaddr_in ma;
    char ip[20];
    ifstream iface_file, rout_file, host_file, port_file, addr_file;
    string port_fname, addr_fname;
    char lan_port[10], lan_addr[20], 
         temp_ipaddr1[20], temp_ipaddr2[20], temp_ipaddr3[20];
    vector<Iface> ifaces; 
    vector<Rtable> rtables;
    vector<Host> hosts;
    Iface temp_i; 
    Rtable temp_r;
    Host temp_h;

    // Verify the correct number of arguments was provided
    if(argc != 5)
    {
        cout << "Usage: chatclient <interface> <routingtable> <hostname>\n";
        return 1;
    }

    iface_file.open(argv[2]);
    rout_file.open(argv[3]);
    host_file.open(argv[4]);

    while (iface_file >> temp_i.ifacename >> temp_ipaddr1 >> temp_ipaddr2
                      >> temp_i.macaddr >> temp_i.lanname)
    {
        temp_i.ipaddr = stoIP(temp_ipaddr1);
        temp_i.netmask = stoIP(temp_ipaddr2);
        ifaces.push_back(temp_i);
    }

    while (rout_file >> temp_ipaddr1 >> temp_ipaddr2 >> temp_ipaddr3 
                     >> temp_r.ifacename)
    {
        temp_r.destsubnet = stoIP(temp_ipaddr1);
        temp_r.nexthop = stoIP(temp_ipaddr2);
        temp_r.mask = stoIP(temp_ipaddr3);
        rtables.push_back(temp_r);
    }

    while (host_file >> temp_h.name >> ip)
    {
        temp_h.addr = stoIP(ip);
        hosts.push_back(temp_h);
    }

    iface_file.close();
    rout_file.close();
    host_file.close();

    // Clear the message buffer
    memset(message, 0, MESSAGE_SIZE*sizeof(char));

    // Set up the client socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    port_fname = string(".") + ifaces[0].lanname + ".port";
    addr_fname = string(".") + ifaces[0].lanname + ".addr";
    port_file.open(port_fname.c_str());
    addr_file.open(addr_fname.c_str());
    port_file >> lan_port;
    addr_file >> lan_addr;
    port_file.close();
    addr_file.close();

    // Set up the sockaddr_in strict 
    ma.sin_family = AF_INET;
    ma.sin_port = htons(atoi(lan_port));
    host = gethostbyname(lan_addr);
    memcpy(&ma.sin_addr, host->h_addr, host->h_length);

    // If connection fails...
    if(connect(sockfd, (struct sockaddr *) &ma, sizeof(ma)) == -1){
	    cerr << "Could not connect to server.\n";
        return -1;
    }

    // Get the port number of the server
    socklen_t maLen = sizeof(ma);
    getsockname(sockfd, (struct sockaddr *) &ma, &maLen);

    maxfd = sockfd + 1;

    cout << "admin: connected to server on '" << argv[1] << "' at '"
         << argv[2] << "' thru '" << ntohs(ma.sin_port) << "'\n";

    while(true){
        FD_ZERO(&readset);

        FD_SET(fileno(stdin), &readset);
        FD_SET(sockfd, &readset);

        // Block until activity on server socket or over stdin
        if(select(maxfd, &readset, NULL, NULL, NULL) > 0)
        {
            // Check for message from server
            if(FD_ISSET(sockfd, &readset))
            {
                // Orderly shutdown
                if(recv(sockfd, message, MESSAGE_SIZE, 0) == 0)
                {
                    cout << "Disconnected from server.\n";
                    shutdown(sockfd, 2);
                    return 0;
                }
                cout << ">>> " << message;
                memset(message, 0, MESSAGE_SIZE);
            }

            // Check for input from stdin
            if(FD_ISSET(fileno(stdin), &readset))
            {
                fgets(message, MESSAGE_SIZE, stdin);
                send(sockfd, message, strlen(message), 0);
                memset(message, 0, MESSAGE_SIZE);
            }
        }
    }

    return 0;
}


IPAddr stoIP (const char * a)
{
    IPAddr IP = 0;

    string temp = "";

    int j = 0;
    for (int i = 0; i < 4; i++)
    {
        while (a[j] != '.')
            temp.push_back(a[j++]);
        // add segment of IP left-shifted by a multiple of 8 bits
        IP += atoi(temp.c_str()) << 8*i;
        temp = ""; 
        j++;
    }

    return IP;
}

string IPtos (IPAddr IP)
{
    string temp;
    char tempstr[10];

    for (int i = 0; i < 4; i++)
    {
        sprintf(tempstr, "%lu", IP&((1<<8)-1));
        // Append first 8 bits of IP to string
        temp.append(tempstr);
        if (i != 3)
            temp.push_back('.');

        IP >>= 8;
    }

    return temp; 
}
