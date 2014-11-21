#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "msg_api.h"
#include "const.h"

struct send_msg_hdr * make_send_hdr(struct send_msg_hdr * hdr, char * ip, uint16_t port, int flag, int len) {
    if(hdr == NULL) {
        hdr = malloc(sizeof(struct send_msg_hdr));
    }

    hdr->dst_ip = inet_addr(ip);
    hdr->dst_port = htons(port);
    hdr->flag = htons(flag);
    hdr->msg_len = htons(len);

    return hdr;
}

struct recv_msg_hdr * make_recv_hdr(struct recv_msg_hdr * hdr, char * ip, uint16_t port, int len) {
    if(hdr == NULL) {
        hdr = malloc(sizeof(struct recv_msg_hdr));
    }

    hdr->src_ip = inet_addr(ip);
    hdr->src_port = htons(port);
    hdr->msg_len = htons(len);

    return hdr;
}

int msg_send(int sockfd, char * dst_ip, uint16_t dst_port, char * msg, int flag) {
    log_debug("Sending message\n");

    struct sockaddr_un tar_addr;
    struct send_msg_hdr * hdr = NULL;
    int len = strlen(msg);
    uint8_t send_dgram[DGRAM_MAX_LEN];
    int sent_size;
    socklen_t tar_len = 0;

    hdr = make_send_hdr(hdr, dst_ip, dst_port, flag, len);
    memcpy(send_dgram, hdr, sizeof(struct send_msg_hdr));
    memcpy(send_dgram + sizeof(struct send_msg_hdr), msg, len);
    sent_size = sizeof(struct send_msg_hdr) + len;

    memset(&tar_addr, 0, sizeof(struct sockaddr_un));
    tar_addr.sun_family = AF_LOCAL;
    strcpy(tar_addr.sun_path, ODR_SUN_PATH);

    tar_len = sizeof(tar_addr);
    if((sent_size = sendto(sockfd, send_dgram, sent_size, 0, (struct sockaddr *) &tar_addr, tar_len)) < 0) {
        log_err("sendto error\n");
        return -1;
    }

    log_debug("Message sent!\n");
    free(hdr);
    return 0;
}

int msg_recv(int sockfd, char * msg, char * src_ip, uint16_t * src_port) {
    log_debug("Blocking to receive message\n");

    int recv_size = 0;
    uint8_t recv_dgram[DGRAM_MAX_LEN];
    struct recv_msg_hdr hdr;
    struct sockaddr_un tar_addr;
    socklen_t tar_len = 0;

    memset(&tar_addr, 0, sizeof(struct sockaddr_un));
    tar_addr.sun_family = AF_LOCAL; // needed ?
    strcpy(tar_addr.sun_path, ODR_SUN_PATH);

    if((recv_size = recvfrom(sockfd, recv_dgram, (size_t) DGRAM_MAX_LEN, 0, (struct sockaddr *) &tar_addr, &tar_len)) < 0) { // should be NULL, NULL?
        log_err("recvfrom error");
        return -1;
    }

    memcpy(&hdr, recv_dgram, sizeof(struct recv_msg_hdr));
    memcpy(msg, recv_dgram + sizeof(struct recv_msg_hdr), recv_size - sizeof(struct recv_msg_hdr));

    src_ip = inet_ntoa((struct in_addr){hdr.src_ip});
    *src_port = ntohs(hdr.src_port);
    memcpy(msg, recv_dgram + sizeof(struct recv_msg_hdr), ntohs(hdr.msg_len));
    msg[ntohs(hdr.msg_len)] = 0;

    log_debug("Message received!\n");
    return 0;
}
