#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "utils.h"

typedef unsigned char u_char;

int
main(int argc, char **argv)
{
	int family, doaliases;

	if (argc != 3)
		err_quit("usage: prifinfo_plus <inet4|inet6> <doaliases>", 1);

	if (strcmp(argv[1], "inet4") == 0)
		family = AF_INET;
	else if (strcmp(argv[1], "inet6") == 0)
		family = AF_INET6;
	else
		err_quit("invalid <address-family>", 1);
	doaliases = atoi(argv[2]);

	dump_ifi_info(family, doaliases);
	return 0;
}
