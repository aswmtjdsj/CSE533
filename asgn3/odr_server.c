#include "log.h"
#include "mainloop.h"
#include "const.h"
#include "odr_protocol.h"

void data_callback() {

}

int main(int argc, const char **argv) {

	log_server_init(argc, argv);
	log_debug("Test log\n");

    char local_host_name[HOST_NAME_MAX_LEN];
    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        my_err_quit("gethostname error");
    }

    log_info("Current node: <%s>\n", local_host_name);
    log_info("ODR Process started and gonna create UNIX domain datagram socket!\n");

    void * ml = mainloop_new();

    struct odr_protocol * op = odr_protocol_init(ml, data_callback, STALE_SEC, get_ifi_info(AF_INET, 0));

    mainloop_run(ml);

    log_info("ODR Process quiting!\n");
    free(ml);

	return 0;
}
