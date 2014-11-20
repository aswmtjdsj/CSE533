#ifndef CONST_H
#define CONST_H

#include <stdio.h>
#include <sys/un.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include "utils.h"
#include "log.h"
#include "msg_api.h"
#include "mainloop.h"

#define HOST_NAME_MAX_LEN 32
#define INPUT_BUF_MAX_LEN 20
#define MSG_MAX_LEN 50
#define IP_P_MAX_LEN 20
#define DGRAM_MAX_LEN 512
#define SEND_MAX_TIMEOUT 5

#define ODR_SUN_PATH "ODR_path_xiangyu"
#define TIM_SERV_SUN_PATH "time_path_xiangyu"
#define TIM_SERV_PORT 19326
#define SUN_PATH_MAX_LEN 100

#define EN_REDISCOVER 1
#define NON_REDISCOVER 0

#define STALE_SEC 3

#endif
