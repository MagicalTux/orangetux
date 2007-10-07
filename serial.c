#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "modem.h"
#include "serial.h"

static int serial;

static char serial_read_cache[4096];
static int serial_read_cache_pos = 0;

static int orangetux_serial_fgets(char *buf, int bufsize) {
	// read data and return if we got something
	int len, i;
	if (serial_read_cache_pos < 4096) {
		len = read(serial, (void*)&serial_read_cache[serial_read_cache_pos], 4096 - serial_read_cache_pos);
		if (len > 0) {
//			write(0, (void*)&serial_read_cache[serial_read_cache_pos], len);
			serial_read_cache_pos += len;
		}
	}
	if (serial_read_cache_pos == 0) return 0;
	// check for \r or \n
	for (i=0;i<serial_read_cache_pos;i++) {
		if (((serial_read_cache[i]=='\n') || (serial_read_cache[i]=='\r')) || (i>=(bufsize-1))) {
			memcpy(buf, &serial_read_cache, i+1);
			memmove(&serial_read_cache, &serial_read_cache[i+1], 4096-(i+1));
			serial_read_cache_pos -= i+1;
			*(buf+i+1) = 0;
			return i+1;
		}
	}
	return 0;
}

static int orangetux_serial_fgets_wait(char *buf, int bufsize, int timeout) {
	int len;
	fd_set fds;
	FD_ZERO(&fds);
	struct timeval tv;
	tv.tv_sec = timeout;
	int i;
	while(1) {
		len = orangetux_serial_fgets(buf, bufsize);
		if (len > 0) return len;
		FD_SET(serial, &fds);
		i = select(serial+1, &fds, NULL, NULL, &tv);
		if (i == 0) return 0;
	}
}

static bool orangetux_serial_wait_ok(int timeout) {
	char buf[1024];
	while(1) {
		int len = orangetux_serial_fgets_wait((char*)&buf, sizeof(buf), timeout);
		if (len <= 0) return false;
		// scan string
		if ((buf[0] == 'O') && (buf[1] == 'K')) return true;
		orangetux_modem_parse_unsol((char*)&buf);
	}
}

static void orangetux_serial_fputs(char *str) {
	int len = strlen(str);
	int pos = 0;
	int i;
//	printf("Write=%s", str);
	while(pos < len) {
		i = write(serial, str+pos, len-pos);
		if (i <= 0) {
			perror("write");
			exit(2);
		}
		pos += i;
	}
}

char *orangetux_serial_cmd(const char *str, int timeout, bool malloc) {
	static char buf[1024];
	int pos = 0;
	sprintf((char*)&buf, "AT%s\r\n", str);
	orangetux_serial_fputs((char*)&buf); // send command
	while(1) {
		int len = orangetux_serial_fgets_wait((char*)&buf[pos], sizeof(buf), timeout);
		if (len <= 0) return false;
		if ((len == 1) && ((buf[pos] == '\r') || (buf[pos] == '\n'))) continue;
		if (strncmp("OK", (char*)&buf[pos], 2) == 0) {
			break;
		}
		if (strncmp("ERROR", (char*)&buf[pos], 5) == 0) {
			return NULL;
		}
		if (strncmp(str, (char*)&buf[pos], 4) != 0) {
			// let's call it
			if (orangetux_modem_parse_unsol((char*)&buf[pos]))
				continue;
		} else {
			int skip = 0;
			int mode = 0;
			for (int i=0;i<len;i++) {
				char c = buf[pos+i];
				if (c == ':') {
					mode = 1;
					continue;
				}
				if (mode == 0) continue;
				if (c != ' ') {
					skip = i;
					break;
				}
			}
			if (skip > 0) {
				memmove(&buf[pos], &buf[pos+skip], sizeof(buf)-(pos+skip));
				len -= skip;
			}
		}
		pos += len;
	}
	if (pos == 0) return "";
	if (!malloc) {
		buf[pos-1] = 0;
		return (char *)&buf;
	}
	char *res = calloc(pos, sizeof(char));
	memcpy(res, &buf, pos-1); // remove last line break (if any)
	return res;
}

bool orangetux_serial_check(void *data) {
	struct timeval tv;
	int i;
	char buf[1024];
	fd_set fds;
	FD_ZERO(&fds);
	memset(&tv, 0, sizeof(tv));
	FD_SET(serial, &fds);
	i = select(serial+1, &fds, NULL, NULL, &tv);
	if (i==0) return true;
	if (orangetux_serial_fgets((char*)&buf, sizeof(buf)) > 0)
		orangetux_modem_parse_unsol((char*)&buf);
	return true;
}

static bool orangetux_serial_init() {
	orangetux_serial_fputs("AT\r\n");
	if (!orangetux_serial_wait_ok(2)) { close(serial); return false; }
	orangetux_serial_fputs("AT&F\r\n");
	if (!orangetux_serial_wait_ok(5)) { close(serial); return false; }
	orangetux_serial_fputs("ATZ\r\n");
	if (!orangetux_serial_wait_ok(5)) { close(serial); return false; }
	orangetux_serial_fputs("ATE0V1&D2&C1S0=0\r\n");
	if (!orangetux_serial_wait_ok(5)) { close(serial); return false; }
	orangetux_serial_fputs("AT+CREG=2\r\n");
	if (!orangetux_serial_wait_ok(5)) { close(serial); return false; }
	orangetux_serial_fputs("AT+CREG?\r\n"); // just let this one be managed by unsol
	if (!orangetux_serial_wait_ok(5)) { close(serial); return false; }
	return true;
}

bool orangetux_serial_open() {
	serial = open("/dev/noz2", O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
	if (serial == -1) {
		perror("open");
		return true;
	}
	if (!orangetux_serial_init()) return false;
	return true;
}

void orangetux_serial_reset() {
	close(serial);
	sleep(1);
	serial = open("/dev/noz2", O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
	if (!orangetux_serial_init()) exit(3);
}


