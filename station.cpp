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

IPAddr getNextHop(vector<Rtable> rtables, IPAddr dst);
bool GetMAC(IPAddr nexthop, vector<ARP_Cache_Entry> ARP_cache, EtherPkt &ether_pkt);
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
    vector<ARP_Cache_Entry> ARP_cache;
    list<IP_PKT> pending_queue;
    IPAddr nextHop;
    ARP_Cache_Entry Cache_entry;

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

    while (host_file >> temp_h.name >> ip)
    {
        temp_h.addr = stoIP(ip);
        hosts.push_back(temp_h);
    }

    iface_file.close();
    rout_file.close();
    host_file.close();

    for (int i = 0; i < ifaces.size(); i++)
    {
        // Set up the client socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);

        port_fname = string(".") + ifaces[i].lanname + ".port";
        addr_fname = string(".") + ifaces[i].lanname + ".addr";
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
        if(connect(sockfd, (struct sockaddr *) &ma, sizeof(ma)) == -1)
        {
            cerr << "Could not connect to server.\n";
            return -1;
        }
        // Receive "accept" or "reject"
        recv(sockfd, message, 7, 0);
        cout << message << endl;
        if (strncmp("reject", message, 7) == 0)
            exit(0);

        // Get the port number of the server
        socklen_t maLen = sizeof(ma);
        getsockname(sockfd, (struct sockaddr *) &ma, &maLen);

        if (sockfd >= maxfd)
            maxfd = sockfd + 1;

        cout << "admin: connected to bridge["<<i<<"] on port " << ntohs(ma.sin_port) << "'\n";
    }


    // Clear the message buffer
    memset(message, 0, MESSAGE_SIZE*sizeof(char));
    memset(&ether_pkt, 0, sizeof(EtherPkt));

    while(true)
    {
        FD_ZERO(&readset);

        FD_SET(fileno(stdin), &readset);
        FD_SET(sockfd, &readset);

        // Block until activity on server socket or over stdin
        if(select(maxfd, &readset, NULL, NULL, NULL) > 0)
        {
            cout << "Select got pinged\n";
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
                cout <<"Packet from server " << ether_pkt.src << "->" << ether_pkt.dst << endl;

		        if (ether_pkt.type == TYPE_IP_PKT)
		        {
                    cout << "IP packet\n";// << IPtos(IP_pkt.dstip) << endl;
                    recv(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                    if (IP_pkt.dstip == ifaces[0].ipaddr)
                    {
                        cout << "Message of size " << IP_pkt.length << " received\n";
                        cout << ">>> " << IP_pkt.data << endl;
                    }
        		    else if (!strcmp(argv[1], "-route"))
        		    {
        		    	// Router stuff
                        strncpy(ether_pkt.src, ifaces[0].macaddr, 18);
                        
                        // Get next hop IP
                        IP_pkt.nexthop = getNextHop(rtable, IP_pkt.dstip);
                        //for (int i = 0; i < rtables.size(); i++)
                        //{
                        //}
                            
                        // look up in ARP cache
                        if (GetMAC(IP_pkt.nexthop, ARP_cache, ether_pkt))
                        {
                            ether_pkt.type = TYPE_IP_PKT;
                        }
                        else
                        {
                            pending_queue.push_back(IP_pkt);

                            ether_pkt.type = TYPE_ARP_PKT;

                            ARP_pkt.op = ARP_REQUEST;
                            ARP_pkt.srcip = ifaces[0].ipaddr;
                            ARP_pkt.dstip = IP_pkt.nexthop;
                            strncpy(ARP_pkt.srcmac, ether_pkt.src, 18);
                        }
                        // if in ARP cache, send IP
                        // otherwise, send ARP, add to pending queue
                        send(sockfd, &ether_pkt, sizeof(EtherPkt), 0);

                        // Send ARP or IP
        		        if (ether_pkt.type == TYPE_IP_PKT)
        	                send(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                		else if (ether_pkt.type == TYPE_ARP_PKT)
                            send(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
        		    }
        		}
		        else if (ether_pkt.type == TYPE_ARP_PKT)
        		{
                    cout << "ARP packet\n";// << IPtos(ARP_pkt.dstip) << "\n";
                    recv(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
                    if (ARP_pkt.dstip == ifaces[0].ipaddr)
                    {
                        if (ARP_pkt.op == ARP_REQUEST)
                        {
                            cout << "Got ARP request\n";
                            cout << IPtos(ARP_pkt.srcip) << "->" << IPtos(ARP_pkt.dstip) << endl;
                            if (ARP_pkt.dstip == ifaces[0].ipaddr)
                            {
                                cout << "Sending response\n";
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
                                strncpy(ether_pkt.dst, ether_pkt.src, 18);
                                strncpy(ether_pkt.src, ifaces[0].macaddr, 18);

                                Cache_entry.ip = ARP_pkt.dstip;
                                strncpy(Cache_entry.mac, ARP_pkt.dstmac, 18);
                                ARP_cache.push_back(Cache_entry);

                                send(sockfd, &ether_pkt, sizeof(EtherPkt), 0);
                                send(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
                            }
                        }
                        else if (ARP_pkt.op == ARP_RESPONSE)// ARP_RESPONSE
                        {
                            cout << "Got ARP response, updating pending queue\n";
                            if (ARP_pkt.dstip == ifaces[0].ipaddr)
                            {
                                for (list<IP_PKT>::iterator it = pending_queue.begin();
                                    it != pending_queue.end(); ++it)
                                {
                                    cout << "it->nexthop = " << IPtos(it->nexthop)
                                         << ' ' << IPtos(ARP_pkt.srcip) << endl;
                                    if (ARP_pkt.srcip == it->nexthop)
                                    {
                                        ether_pkt.type = TYPE_IP_PKT;
                                        strncpy(ether_pkt.src, ifaces[0].macaddr, 18);
                                        strncpy(ether_pkt.dst, ARP_pkt.srcmac, 18);

                                        Cache_entry.ip = ARP_pkt.srcip;
                                        strncpy(Cache_entry.mac, ARP_pkt.srcmac, 18);
                                        ARP_cache.push_back(Cache_entry);

                                        // Might need to set each component individually
                                        IP_pkt = *it;

                                        pending_queue.erase(it);
                                        cout << "Sending IP packet\n";
                                        send(sockfd, &ether_pkt, sizeof(EtherPkt), 0);
                                        send(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                                    }
                                }
                            }
                        }
                    }
                    else cout << "Not for me.\n";
                }
                else cout << "Something happened.\n";
	        }

            // Check for input from stdin
            if(FD_ISSET(fileno(stdin), &readset))
            {
                char dest[32];
                cin >> dest;

                // Find the destination station's IP
                for (int i = 0; i < hosts.size(); i++)
                {
                    if (strncmp(dest, hosts[i].name, 32) == 0)
                    {
                        cout << "Destination IP = " << IPtos(hosts[i].addr) << endl;
                        IP_pkt.dstip = hosts[i].addr;
                        IP_pkt.nexthop = getNextHop(rtable, IP_pkt.dstip);
                        break;
                    }
                }

		        // Copy over source MAC address
        		strncpy(ether_pkt.src, ifaces[0].macaddr, 18);

                //if (it != ARP_cache.end())
                if (GetMAC(IP_pkt.nexthop, ARP_cache, ether_pkt))
                {
                    //strncpy(ether_pkt.dst, it->second, 18);
                    ether_pkt.type = TYPE_IP_PKT;

        		    IP_pkt.srcip = ifaces[0].ipaddr;
        		    cin.getline(IP_pkt.data, SHRT_MAX);
        		    IP_pkt.length = strlen(IP_pkt.data);
                }
                else 
                {
		            cin.getline(IP_pkt.data, SHRT_MAX);
                    IP_pkt.length = strlen(IP_pkt.data);
        		    IP_pkt.srcip = ifaces[0].ipaddr;
        		    pending_queue.push_back(IP_pkt);

        		    ether_pkt.type = TYPE_ARP_PKT;
                    //ether_pkt.dst[0] = 'x';

                    ARP_pkt.op = ARP_REQUEST;
		            ARP_pkt.srcip = ifaces[0].ipaddr;
                    strncpy(ARP_pkt.srcmac, ether_pkt.src, 18);
        		    ARP_pkt.dstip = IP_pkt.dstip;
                }


                //cin.getline(ether_pkt.dat, SHRT_MAX);
                //ether_pkt.size = strlen(ether_pkt.dat);
                //cout << "Packet dat = " << ether_pkt.dat << endl;
                //cout << ether_pkt.src << " -> " << ether_pkt.dst << endl;

                cout << "Sending to " << IPtos(IP_pkt.dstip) << endl;
                send(sockfd, &ether_pkt, sizeof(EtherPkt), 0);

                // Send ARP or IP
		        if (ether_pkt.type == TYPE_IP_PKT)
	                send(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
        		else if (ether_pkt.type == TYPE_ARP_PKT)
                    send(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
		        // Else case for closed connections
            }

            memset(&ether_pkt, 0, sizeof(EtherPkt));
            memset(&ARP_pkt, 0, sizeof(ARP_PKT));
            memset(&IP_pkt, 0, sizeof(IP_PKT));
        }
    }

    return 0;
}

IPAddr getNextHop(vector<Rtable> rtable, IPAddr dst)
{
    // Find next hop IP address in routing table
    for (int j = 0; j < rtable.size(); j++)
        if((dst & rtable[j].mask) == rtable[j].destsubnet)
            // Return the nexthop in rtable (or dstip if on same LAN)
            return (rtable[j].nexthop ? rtable[j].nexthop : dst);
}


bool GetMAC(IPAddr nexthop, vector<ARP_Cache_Entry> ARP_cache, EtherPkt &ether_pkt)
{
    vector<ARP_Cache_Entry>::iterator it;
    for (it = ARP_cache.begin(); it != ARP_cache.end() && it->ip != nexthop; it++);

    // Copy over source MAC address
    //strncpy(ether_pkt.src, ifaces[0].macaddr, 18);

    if (it != ARP_cache.end())
    {
        strncpy(ether_pkt.dst, it->mac, 18);
        return 1;
        //IP_pkt.dstip = newnode.dstip;
        //IP_pkt.srcip = ifaces[0].ipaddr;
        //cin.getline(IP_pkt.data, SHRT_MAX);
        //IP_pkt.length = strlen(IP_pkt.data);
    }
    else 
    {
        //cin.getline(newnode.data, SHRT_MAX);
        //newnode.length = strlen(newnode.data);
        //IP_pkt.srcip = ifaces[0].ipaddr;
        //pending_queue.push_back(IP_pkt);

        ether_pkt.dst[0] = 'x';

        //ARP_pkt.op = ARP_REQUEST;
        //ARP_pkt.srcip = ifaces[0].ipaddr;
        //strncpy(ARP_pkt.srcmac, ether_pkt.src, 18);
        //ARP_pkt.dstip = IP_pkt.nexthop.;

        return 0;
    }



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
