#include "const.h"
#include <netinet/if_ether.h>

struct tour_list_entry {
	struct sockaddr_in ip_addr;
	struct tour_list_entry * next;
	char host_name[HOST_NAME_MAX_LEN];
} * tour_list;

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

int main(int argc, const char **argv) {

	tour_list = NULL;
	struct tour_list_entry ** p_tail = &tour_list;
	int code_flag = EXIT_SUCCESS;

	char local_name[HOST_NAME_MAX_LEN], tmp_str[HOST_NAME_MAX_LEN];
    if(gethostname(tmp_str, sizeof(tmp_str)) < 0) {
        my_err_quit("gethostname error");
    }
    strcpy(local_name, tmp_str);

	if(argc < 2) {

		log_info("No names detected! "
				"Current node <%s> is not the source node!\n", local_name);

	} else {
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
		my_err_quit("socket error!");
	}

ALL_DONE:
	log_info("Gonna close ... Clearing junk ...\n");
	while(tour_list != NULL) {
		struct tour_list_entry * temp = tour_list->next;
		free(tour_list);
		tour_list = temp;
	}
	return code_flag;
}
