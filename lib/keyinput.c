#include "keyinput.h"
#include "log.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>



static int fd = -1;
void keyinput_init(){
    fd = open("/dev/input/event0", O_RDONLY);
    if(fd < 0){
        log_error("unable to open /dev/input/event0");
        return;
    }

    if(fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        log_error("fcntl failed");
        return;
    }

}

static int g_last_key = -1;
int keyinput_get_key(){
    struct input_event in = { 0 };
    while(1){
        int bytes_read = read(fd, &in, sizeof(in));
        if(bytes_read <= 0){
            return g_last_key;
        }
        if(in.type != EV_KEY){
            continue;
        }
        if(in.value == 1) {
            g_last_key = in.code;
            return g_last_key;
        }
        if(in.value == 0) {
            g_last_key = -1;
        }
    }
}