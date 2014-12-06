#ifndef CONST_H
#define CONST_H

#include <stdio.h>
#include <sys/un.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include "utils.h"
#include "log.h"
#include "api.h"
#include "mainloop.h"

#define IPPROTO_XIANGYU 238
#define HOST_NAME_MAX_LEN 32
#define IP_HDR_ID 23891
#define MAX_IP_IN_PAYLOAD 32
#define MULTICAST_IP "224.0.0.2"
#define MULTICAST_PORT 17828
#define ID "syuxuan"
/*
#define INPUT_BUF_MAX_LEN 20
#define MSG_MAX_LEN 50
#define IP_P_MAX_LEN 20
#define DGRAM_MAX_LEN 512
#define ODR_MSG_MAX_LEN 1024

#define MAX_PORT_NUM 65536

#define SEND_MAX_TIMEOUT 5

#define ODR_SUN_PATH "/tmp/ODR_path_" ID
#define TIM_SERV_SUN_PATH "/tmp/time_path_" ID
#define CLI_SUN_PATH "/tmp/client_" ID "_XXXXXX"
#define TIM_SERV_PORT 19326
#define SUN_PATH_MAX_LEN 100

#define EN_REDISCOVER 1
#define NON_REDISCOVER 0

#define TIM_LIV_NON_PERMAN 5
*/

#endif
