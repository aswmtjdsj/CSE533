#include "log.h"

int main(int argc, const char **argv) {

	log_server_init(argc, argv);
	log_info("Test log\n");

	return 0;
}
