#include "const.h"

struct tour_list_entry {
	struct sockaddr_in ip_addr;
	struct tour_list_entry * next;
	char host_name[HOST_NAME_MAX_LEN];
} * tour_list;

struct ip_payload {
    uint32_t mcast_ip;
    uint16_t mcast_port;
    uint8_t ip_num;
    uint8_t cur_pt;
    uint32_t ip_list[MAX_IP_IN_PAYLOAD];
};

void show_tour_list() {
	struct tour_list_entry * temp_entry = tour_list;
	int cnt = 0;
	log_debug("tour_list>\n");
	log_debug("\t#id\thost_name\tip_addr\n");
	while(temp_entry != NULL) {
		log_debug("\t%d\t%s\t%s\n", cnt, temp_entry->host_name, inet_ntoa((temp_entry->ip_addr).sin_addr));
		cnt++;
		temp_entry = temp_entry->next;
	}
}

struct iphdr * make_ip_hdr(struct iphdr * hdr, uint32_t payload_len, uint16_t id, uint32_t src_addr, uint32_t dst_addr) {
    // struct iphdr * hdr = (struct iphdr *) malloc(sizeof(struct iphdr));
    hdr->ihl = 5;
    hdr->version = 4;
    // hdr->tos
    hdr->tot_len = htons(sizeof(struct iphdr) + payload_len);
    hdr->id = htons(id);
    // hdr->frag_off
    // hdr->ttl
    hdr->protocol = IPPROTO_XIANGYU; // need network byte order?
    hdr->saddr = htonl(src_addr);
    hdr->daddr = htonl(dst_addr);
    // hdr->check = in_cksum(); // do I need this?
    return hdr;
}

struct ip_payload * make_ip_payload(struct ip_payload * payload, uint32_t m_ip, uint32_t m_port, uint8_t cur_pointer, struct tour_list_entry * tour_list, struct ip_payload * prev) {
    // struct ip_payload * payload = NULL;
    if(!((tour_list == NULL) ^ (prev == NULL))) {
        log_err("tour list and previous payload could not be NULL or valid at the same time!\n");
        exit(EXIT_FAILURE);
    }
    if(prev == NULL) {
         // payload = (struct ip_payload *) malloc(sizeof(struct ip_payload));
         payload->mcast_ip = htonl(m_ip);
         payload->mcast_port = htons(m_port);
         payload->cur_pt = htons(cur_pointer);

         payload->ip_num = 0;
         struct tour_list_entry * temp_entry = tour_list;
         while(temp_entry != NULL) {
             payload->ip_list[payload->ip_num++] = (temp_entry->ip_addr).sin_addr.s_addr; // already network byte order
             temp_entry = temp_entry->next;
         }
         payload->ip_num = htons(payload->ip_num);
    } else {
        payload = prev;
        if(ntohs(payload->cur_pt) + 1 >= ntohs(payload->ip_num)) {
            log_err("This is the last node of tour list! No need to make new ip packet for sending!\n");
            exit(EXIT_FAILURE);
        }
        payload->cur_pt = htons(ntohs(payload->cur_pt) + 1);
    }

    return payload;
}

int main(int argc, const char **argv) {

	tour_list = NULL;
	struct tour_list_entry ** p_tail = &tour_list;
	int code_flag = EXIT_SUCCESS;
    int source_node_flag = 0;
    int ret = 0; // return status

	char local_name[HOST_NAME_MAX_LEN], tmp_str[HOST_NAME_MAX_LEN];
    if(gethostname(tmp_str, sizeof(tmp_str)) < 0) {
        my_err_quit("gethostname error");
    }
    strcpy(local_name, tmp_str);

	if(argc < 2) {

		log_info("No names detected! "
				"Current node <%s> is not the source node!\n", local_name);

	} else {
        // this is source node, mark it
        source_node_flag = 1;
		// parse vm node seq
		log_info("Name sequence detected! "
				"Current node <%s> is the source node!\n", local_name);
		log_debug("Parsing node name sequence ...\n");
		int i;
		struct hostent * i_host = NULL;
		// should add source node itself into tour list
		for(i = 1; i <= argc; i++) {
			// check same node should not appear consequently
			if(i > 1 && i < argc && strcmp(argv[i], argv[i-1]) == 0) {
				log_err("Same node \"%s\" should not appear consecutively "
						"in the node sequence!\n", argv[i]);
				code_flag = EXIT_FAILURE;
				goto ALL_DONE;
			}

			// get dest host by name
			if((i_host = gethostbyname((i==argc)?local_name:argv[i])) == NULL) {
				switch(h_errno) {
					case HOST_NOT_FOUND:
						log_err("host %s not found!\n", (i==argc)?local_name:argv[i]);
						break;
					case NO_ADDRESS:
						// case NO_DATA:
						log_err("host %s is valid, but does not have an IP address!\n",
								(i==argc)?local_name:argv[i]);
						break;
					case NO_RECOVERY:
						log_err("A nonrecoverable name server error occurred.!\n");
						break;
					case TRY_AGAIN:
						log_err("A temporary error occurred on "
								"an authoritative name server. Try again later.\n");
						break;
				}

				log_err("gethostbyname error!\n");
				code_flag = EXIT_FAILURE;
				goto ALL_DONE;
			}

			if(i != argc) {
				struct tour_list_entry * cur_entry = (struct tour_list_entry *) malloc(sizeof(struct tour_list_entry));
				(cur_entry->ip_addr).sin_addr = *((struct in_addr *)(i_host->h_addr_list[0]));
				strcpy(cur_entry->host_name, argv[i]);
				cur_entry->next = NULL;
				*p_tail = cur_entry;
				p_tail = &((*p_tail)->next);
			} else { // add source node into list
				struct tour_list_entry * prev = tour_list;
				tour_list = (struct tour_list_entry *) malloc(sizeof(struct tour_list_entry));
				(tour_list->ip_addr).sin_addr = *((struct in_addr *)(i_host->h_addr_list[0]));
				strcpy(tour_list->host_name, local_name);
				tour_list->next = prev;
			}
		}
		show_tour_list();
	}

	int sock_rt, sock_pg_reply, sock_pg_send, sock_un; // unix sock, need think

	// create route traversal socket, IP RAW, with self-defined protocol value
	if((sock_rt = socket(AF_INET, SOCK_RAW, IPPROTO_XIANGYU)) < 0) {
		my_err_quit("socket error");
	}

	// set IP_HDRINCL for rt socket
	const int on_flag = 1;
	if(setsockopt(sock_rt, IPPROTO_IP, IP_HDRINCL, &on_flag, sizeof(on_flag)) < 0) {
		my_err_quit("setsockopt error");
	}

	// create ping reply socket, IP RAW, with ICMP protocol
	if((sock_pg_reply = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
		my_err_quit("socket error");
	}

	// create ping reply socket, IP RAW, with ICMP protocol
	if((sock_pg_send = socket(AF_PACKET, SOCK_RAW, ETH_P_IP)) < 0) {
		my_err_quit("socket error");
	}

    // for source node, it should actively send
    if(source_node_flag) {
        unsigned char * packet = malloc(sizeof(struct iphdr) + sizeof(struct ip_payload));
        struct iphdr * i_hdr = (struct iphdr *) packet;
        struct ip_payload * i_payload = (struct ip_payload *) (i_hdr + 1);
        struct in_addr m_addr;
        memset(&m_addr, 0, sizeof(m_addr));
        if((ret = inet_pton(AF_INET, MULTICAST_IP, &m_addr)) < 0) {
            my_err_quit("inet_pton error");
        }
        i_payload = make_ip_payload(i_payload, m_addr.s_addr, MULTICAST_PORT, 0, tour_list, NULL);
        uint32_t s_addr = (tour_list->ip_addr).sin_addr.s_addr, d_addr = (tour_list->next->ip_addr).sin_addr.s_addr;
        i_hdr = make_ip_hdr(i_hdr, sizeof(struct ip_payload), IP_HDR_ID, s_addr, d_addr);

        int n = 0;
        socklen_t addr_len = sizeof(tour_list->ip_addr);
        tour_list->ip_addr.sin_family = AF_INET; // should be set
        log_debug("gonna send ip packet via rt sock!\n");
        if((n = sendto(sock_rt, packet, ntohs(i_hdr->tot_len), 0, (struct sockaddr *) &(tour_list->ip_addr), addr_len)) < 0) {
            my_err_quit("sendto error");
        }
    }

	void * ml = mainloop_new();
	mainloop_run(ml);

ALL_DONE:
	log_info("Gonna close ... Clearing junk ...\n");
	while(tour_list != NULL) {
		struct tour_list_entry * temp = tour_list->next;
		free(tour_list);
		tour_list = temp;
	}
	free(ml);
	return code_flag;
}
