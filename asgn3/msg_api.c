#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "msg_api.h"
#include "const.h"

struct send_msg_hdr * make_send_hdr(struct send_msg_hdr * hdr, char * ip,
				    uint16_t port, int flag, int len) {
	hdr->dst_ip = inet_addr(ip);
	hdr->dst_port = htons(port);
	hdr->flag = htons(flag);
	hdr->msg_len = htons(len);

	return hdr;
}

void make_send_msg(uint8_t * send_msg, struct send_msg_hdr * s_hdr,
		   void * payload, int payload_len, int * send_msg_len) {
	memcpy(send_msg, s_hdr, sizeof(struct send_msg_hdr));
	memcpy(send_msg + sizeof(struct send_msg_hdr), payload, payload_len);
	*send_msg_len = sizeof(struct send_msg_hdr) + payload_len;

	char msg_debug[MSG_MAX_LEN];
	strncpy(msg_debug, payload, payload_len);
	msg_debug[payload_len] = 0;
	log_debug("send_msg: {hdr: {dst_ip: \"%s\", dst_port: %u, flag: %d, "
	    "msg_len: %d}, payload: \"%s\", len: %d}\n",
	    inet_ntoa((struct in_addr){s_hdr->dst_ip}), ntohs(s_hdr->dst_port),
	    ntohs(s_hdr->flag), ntohs(s_hdr->msg_len), payload, *send_msg_len);
}

struct recv_msg_hdr *
make_recv_hdr(struct recv_msg_hdr * hdr, uint32_t ip,
	      uint16_t port, int len) {
	hdr->src_ip = ip;
	hdr->src_port = htons(port);
	hdr->msg_len = htons(len);

	return hdr;
}

void make_recv_msg(uint8_t * recv_msg, struct recv_msg_hdr * r_hdr,
		   void * payload, int payload_len, int * recv_msg_len) {
	memcpy(recv_msg, r_hdr, sizeof(struct recv_msg_hdr));
	memcpy(recv_msg + sizeof(struct recv_msg_hdr), payload, payload_len);
	*recv_msg_len = sizeof(struct recv_msg_hdr) + payload_len;

	log_debug("recv_msg: {hdr: {src_ip: \"%s\", src_port: %u, msg_len: %d}"
	    ", payload: \"%s\", len: %d}\n",
	    inet_ntoa((struct in_addr){r_hdr->src_ip}), ntohs(r_hdr->src_port),
	    ntohs(r_hdr->msg_len), payload, *recv_msg_len);
}

int
msg_send(int sockfd, char * dst_ip, uint16_t dst_port, char * msg, int flag) {
	log_debug("Sending message\n");

	struct sockaddr_un tar_addr;
	struct send_msg_hdr hdr;
	int len = strlen(msg);
	uint8_t send_dgram[DGRAM_MAX_LEN];
	int sent_size = 0;
	socklen_t tar_len = 0;

	make_send_msg(send_dgram,
		      make_send_hdr(&hdr, dst_ip, dst_port, flag, len),
		      msg, len, &sent_size);

	memset(&tar_addr, 0, sizeof(struct sockaddr_un));
	tar_addr.sun_family = AF_LOCAL;
	strcpy(tar_addr.sun_path, ODR_SUN_PATH);

	tar_len = sizeof(tar_addr);
	if(sendto(sockfd, send_dgram, sent_size, 0,
	    (struct sockaddr *) &tar_addr, tar_len) < 0) {
		log_err("sendto error %s\n", strerror(errno));
		return -1;
	}

	log_debug("Message sent!\n");
	return 0;
}

int msg_recv(int sockfd, char * msg, char * src_ip, uint16_t * src_port) {
	log_debug("Blocking to receive message\n");

	int recv_size = 0;
	uint8_t recv_dgram[DGRAM_MAX_LEN];
	struct recv_msg_hdr *hdr;
	struct sockaddr_un tar_addr;
	socklen_t tar_len = 0;

	memset(&tar_addr, 0, sizeof(struct sockaddr_un));
	tar_addr.sun_family = AF_LOCAL; // needed ?
	strcpy(tar_addr.sun_path, ODR_SUN_PATH);

	recv_size = recvfrom(sockfd, recv_dgram, (size_t) DGRAM_MAX_LEN, 0,
			     (struct sockaddr *) &tar_addr, &tar_len);
	if (recv_size < 0) { // should be NULL, NULL?
		log_err("recvfrom error");
		return -1;
	}

	hdr = (void *)recv_dgram;

	strcpy(src_ip, inet_ntoa((struct in_addr){hdr->src_ip}));
	*src_port = ntohs(hdr->src_port);
	memcpy(msg, hdr+1, ntohs(hdr->msg_len));
	msg[ntohs(hdr->msg_len)] = 0;

	log_debug("msg_recv, source ip: %s, source port: %u\n",
	    src_ip, *src_port);
	log_debug("Message received!\n");
	return 0;
}
