#ifndef INIT_INTERNAL_H
#define INIT_INTERNAL_H
#define _GNU_SOURCE
#include"init.h"
#include<sys/socket.h>
#define DEFAULT_INITD _PATH_RUN"/initd.sock"

// initd packet magic
#define INITD_MAGIC0 0xEF
#define INITD_MAGIC1 0x99

enum init_status{
	INIT_RUNNING      =0xCE01,
	INIT_BOOT         =0xCE02,
	INIT_SHUTDOWN     =0xCE03,
};
extern enum init_status status;

enum init_action{
	ACTION_NONE       =0xBE00,
	ACTION_OK         =0xBE01,
	ACTION_FAIL       =0xBE02,
	ACTION_POWEROFF   =0xBE03,
	ACTION_HALT       =0xBE04,
	ACTION_REBOOT     =0xBE05,
	ACTION_SWITCHROOT =0xBE06,
};
extern enum init_action action;
extern char action_data[256];

struct init_msg{
	unsigned char magic0,magic1;
	enum init_action action;
	union{
		int ret;
		char data1[63];
		char data2[63];
		char data3[63];
		char data4[63];
	};
};
#endif