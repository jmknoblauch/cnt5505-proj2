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
#include <map>
#include <list>
#include "ip.h"

#define MESSAGE_SIZE 200

using namespace std;

void stoMac(MacAddr mac, const char* str);
void Mactos(MacAddr mac, const char* str);
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
         temp_ipaddr1[20], temp_ipaddr2[20], temp_ipaddr3[20], temp_macaddr[20];
    vector<Iface> ifaces; 
    vector<Rtable> rtable;
    vector<Host> hosts;
    Iface temp_i; 
    Rtable temp_r;
    Host temp_h;
    EtherPkt ether_pkt;
    IP_PKT IP_pkt;
    ARP_PKT ARP_pkt;
    char input [SHRT_MAX*sizeof(char)];
    //char buffer[SHRT_MAX];
    map<IPAddr, MacAddr> ARP_Cache;
    list<IP_PKT> pending_queue;
    IPAddr nextHop;
    IP_PKT newnode;

    // Verify the correct number of arguments was provided
    if(argc != 5)
    {
        cout << "Usage: chatclient <flags> <interface> <routingtable> <hostname>\n";
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
        rtable.push_back(temp_r);
    }

    while (host_file >> temp_h.name >> ip >> temp_h.macaddr)
    {
        temp_h.addr = stoIP(ip);
        hosts.push_back(temp_h);
    }

    iface_file.close();
    rout_file.close();
    host_file.close();

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

    recv(sockfd, message, 7, 0);

    cout << message << endl;

    if (message[0] == 'r')
        exit(0);

    // Get the port number of the server
    socklen_t maLen = sizeof(ma);
    getsockname(sockfd, (struct sockaddr *) &ma, &maLen);

    maxfd = sockfd + 1;

    cout << "admin: connected to server on '" << argv[1] << "' at '"
         << argv[2] << "' thru '" << ntohs(ma.sin_port) << "'\n";

    // Clear the message buffer
    memset(message, 0, MESSAGE_SIZE*sizeof(char));
    memset(&ether_pkt, 0, sizeof(EtherPkt));

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
                if(recv(sockfd, &ether_pkt, sizeof(EtherPkt), 0) == 0)
                {
                    cout << "Disconnected from server.\n";
                    //shutdown(sockfd, 2);
                    return 0;
                }
		if (ether_pkt.type == TYPE_IP_PKT)
		{
                    recv(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                    if (ifaces[0].ipaddr == IP_pkt.dstip)
                    {
                        cout << "Message of size " << IP_pkt.length << " received\n";
                        cout << ">>> " << IP_pkt.data << endl;
                    }
		    else if (!strcmp(argv[1], "-route"))
		    {
		    	// Router stuff
		    }
		}
		else if (ether_pkt.type == TYPE_ARP_PKT)
		{
		    recv(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
		    if (ARP_pkt.dstip == ifaces[0].ipaddr)
		    {
		        if (ARP_pkt.op == ARP_REQUEST)
		        {
			    if (ARP_pkt.dstip == ifaces[0].ipaddr)
			    {
			        MacAddr temp_mac;

				ARP_pkt.op = ARP_RESPONSE;

				// Swap ARP destination and source IP's
				IPAddr temp_ip = ARP_pkt.dstip;
				ARP_pkt.dstip = ARP_pkt.srcip;
				ARP_pkt.srcip = temp_ip;

				// Swap ARP destination and source macs
				strncpy(ARP_pkt.dstmac, ARP_pkt.srcmac, 18);
				strncpy(ARP_pkt.srcmac, ifaces[0].macaddr, 18);
				  
				// Swap ether destination and source macs
				strncpy(temp_mac, ether_pkt.dst, 18);
				strncpy(ether_pkt.dst, ether_pkt.src, 18);
				strncpy(ether_pkt.src, temp_mac, 18);

				send(sockfd, &ether_pkt, sizeof(EtherPkt), 0);
				send(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
			    }
			}
		        else // ARP_RESPONSE
		        {
			    if (ARP_pkt.dstip == ifaces[0].ipaddr)
			    {
			    	for (list<IP_PKT>::iterator it = pending_queue.begin();
				     it != pending_queue.end(); ++it)
				{
				    if (ARP_pkt.srcip == it->nexthop)
				    {
					ether_pkt.type = TYPE_IP_PKT;
				    	strncpy(ether_pkt.src, ifaces[0].macaddr, 18);
					strncpy(ether_pkt.dst, ARP_pkt.srcmac, 18);

					// Might need to set each component individually
					IP_pkt = *it;

					pending_queue.erase(it);
				    }
				
				}
		                // Queue stuff
			    }
		        }
		    }
		    else if (!strcmp(argv[1], "-route"))
		    {
		    	// Router stuff
		    }
                }
	    }

            // Check for input from stdin
            if(FD_ISSET(fileno(stdin), &readset))
            {
                char dest[32];
                cin >> dest;

                for (int i = 0; i < hosts.size(); i++)
                {
                    if (strncmp(dest, hosts[i].name, 32) == 0)
                    {
                        // Replace this once ARP cache is working.
                        //strncpy(ether_pkt.dst, hosts[i].macaddr, 18);

			newnode.dstip = hosts[i].addr;
                        // Find next hop IP address in routing table
                        for (int j = 0; j < rtable.size(); j++)
                        {
                            if(hosts[i].addr & rtable[j].mask == rtable[j].destsubnet)
                            {
                              	newnode.nexthop = rtable[j].nexthop;
                                break;
                            }
                        }
                    }
                }
                
                // Check if next hop IP is already in ARP_Cache
                map<IPAddr, MacAddr>::iterator it = ARP_Cache.find(newnode.nexthop);

		// Copy over source MAC address
		strncpy(ether_pkt.src, ifaces[0].macaddr, 18);

                if (it != ARP_Cache.end())
                {
                    strncpy(ether_pkt.dst, it->second, 18);
                    ether_pkt.type = TYPE_IP_PKT;

		    IP_pkt.dstip = newnode.dstip;
		    IP_pkt.srcip = ifaces[0].ipaddr;
		    cin.getline(IP_pkt.data, SHRT_MAX);
		    IP_pkt.length = strlen(IP_pkt.data);
                }
                else 
                {
		    cin.getline(newnode.data, SHRT_MAX);
                    newnode.length = strlen(newnode.data);
		    newnode.srcip = ifaces[0].ipaddr;
		    pending_queue.push_back(newnode);

		    ether_pkt.type = TYPE_ARP_PKT;

                    ARP_pkt.op = ARP_REQUEST;
		    ARP_pkt.srcip = ifaces[0].ipaddr;
                    strncpy(ARP_pkt.srcmac, ether_pkt.src, 18);
		    ARP_pkt.dstip = newnode.dstip;
                }


                //cin.getline(ether_pkt.dat, SHRT_MAX);
                //ether_pkt.size = strlen(ether_pkt.dat);
                //cout << "Packet dat = " << ether_pkt.dat << endl;
                //cout << ether_pkt.src << " -> " << ether_pkt.dst << endl;

                send(sockfd, &ether_pkt, sizeof(EtherPkt), 0);

                // Send ARP or IP
		if (ether_pkt.type == TYPE_IP_PKT)
	            send(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
		else if (ether_pkt.type == TYPE_ARP_PKT)
                    send(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
		// Else case for closed connections
            }

            memset(&ether_pkt, 0, sizeof(EtherPkt));
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
