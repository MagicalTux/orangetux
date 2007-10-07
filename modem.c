#include <stdbool.h>
#include <stdint.h>

#include <string.h>
#include <stdio.h>
#include <unistd.h> /* sleep */

#include "serial.h"
#include "modem.h"
#include "main.h"

static struct {
	char *manufacturer;
	char *model;
	char *revision;
	char *imei;
	char *serial;
	bool planemode;
	char *imsi;
	char *status;
	char mode;
	int radiotype;
	uint16_t lap, cid;
} modem;

bool orangetux_modem_end_ussd() {
	if (orangetux_serial_cmd("+CUSD=2", 5, false) == NULL) return false;
	return true;
}

bool orangetux_modem_set_network(char *net) {
	char buf[256];
	sprintf((char*)&buf, "+CGDCONT=1,\"IP\",\"%s\",\"\",0,0", net);
	if (orangetux_serial_cmd((char*)&buf, 5, false) == NULL) return false;
	return true;
}

bool orangetux_modem_send_ussd(const char *data) {
	char cmd[255];
	if (strlen(data)>187) return false; // TODO: check this value
	sprintf((char*)&cmd, "+CUSD=1,\"%s\",15", data);
	char *res = orangetux_serial_cmd((char*)&cmd, 5, false);
	if (res == NULL) return false;
	return true;
}

char *orangetux_modem_cpin() {
	if (modem.planemode) return NULL;
	char *auth = orangetux_serial_cmd("+CPIN?", 5, false);
	if (auth == NULL) return NULL;
	if (strcmp(auth, "READY") == 0) return NULL;
	return auth;
}

bool orangetux_modem_get_planemode() {
	if (*orangetux_serial_cmd("+CFUN?", 5, false) == '0') return true; // currently in plane mode
	return false;
}

void orangetux_modem_set_planemode(bool enable) {
	if (enable) {
		if (modem.planemode) return; // already in plane mode
		orangetux_serial_cmd("+CFUN=0,0", 5, false);
		modem.planemode = true;
	} else {
		if (!modem.planemode) return; // already out of plane mode
		orangetux_serial_cmd("+CFUN=1,1", 5, false);
		modem.planemode = false;
		sleep(2); // let modem recover
	}
	orangetux_serial_reset();
}

bool orangetux_modem_send_cpin(char *cpin) {
	char cmd[32];
	if (strlen(cpin)>16) return false; // ?! (avoid buffer overflow)
	sprintf((char*)&cmd, "+CPIN=\"%s\"", cpin);
	char *res = orangetux_serial_cmd((char*)&cmd, 5, false);
	if (res == NULL) return false;
	return true;
}

void orangetux_modem_update_status() {
	if (modem.planemode) {
		orangetux_main_set_tooltip("Plane mode enabled");
		return;
	}
	int a,b;
	int level;
	char res[128];
	char net[64];
	char *radiomode;

	char *tmp = orangetux_serial_cmd("+COPS?", 5, false);
	if (tmp == NULL) return;
	int r = sscanf(tmp, "%d,%d,\"%[^\"]\",%d", &a, &b, (char*)&net, &modem.radiotype);
	if (r < 3) return;
	if (r == 3) modem.radiotype = 0; // not supported by this device ?

	tmp = orangetux_serial_cmd("+CSQ", 5, false);
	if (tmp == NULL) return;
	r = sscanf(tmp, "%d,%d", &a, &b);
	if (r < 1) return;
	level = ((float)a) * 100 / 31;

	switch(modem.radiotype) {
		case 0: radiomode = "2G"; break;
		case 2: radiomode = "3G"; break;
		default: radiomode = "unknown"; break;
	}

	sprintf((char*)&res, modem.status, (char*)&net, level, modem.lap, modem.cid, radiomode);
	orangetux_main_set_tooltip((char*)&res);
}

void orangetux_modem_init() {
	modem.manufacturer = orangetux_serial_cmd("+CGMI", 5, true);
	modem.model = orangetux_serial_cmd("+CGMM", 5, true);
	modem.revision = orangetux_serial_cmd("+CGMR", 5, true);
	modem.imsi = orangetux_serial_cmd("+CIMI", 5, true);
	modem.imei = orangetux_serial_cmd("+CGSN", 5, true);
	int len=strlen(modem.imei);
	for(int i=0;i<len;i++) {
		if (modem.imei[i] == ',') {
			modem.imei[i] = 0;
			modem.serial = &modem.imei[i+1];
		}
	}
	if (*orangetux_serial_cmd("+CFUN?", 5, false) == '0') {
		modem.planemode = true;
	} else {
		modem.planemode = false;
	}
}

static bool orangetux_unsol_cusd = false;
static char cusd_buffer[4096];
static int cusd_pos;
static int cusd_type;

static void orangetux_cusd_mode(char *str) {
	if (!orangetux_unsol_cusd) {
		str+= 6;
		while(*str == ' ') str++;
		cusd_type = *str - '0';
		str++; // skip number
		str++; // skip ,
		str++; // skip "
		orangetux_unsol_cusd = true;
		cusd_pos = 0;
	}
	int len = strlen(str);
	for (int i=0; i<len; i++) {
		char c = *(str+i);
		if (c == 0) break; // Oo
		char c2 = *(str+i+1);
		if (c == '\\') {
			switch(c2) {
				case '"': case '\\':
					cusd_buffer[cusd_pos++] = c2;
					i++; // skip
					break;
				default:
					cusd_buffer[cusd_pos++] = c;
					break;
			}
			continue;
		}
		if (c == '"') {
			orangetux_unsol_cusd = false;
			break;
		}
		if (c == '\r') c = '\n';
		cusd_buffer[cusd_pos++] = c;
	}
	if (orangetux_unsol_cusd) return;
	cusd_buffer[cusd_pos++] = 0;
	orangetux_main_ussd_callback(cusd_type, (char*)&cusd_buffer);
//	printf("GOT USSD %s\n", (char*)&cusd_buffer);
}

bool orangetux_modem_parse_unsol(char *unsol) {
	if (orangetux_unsol_cusd) { // FIRST!
		orangetux_cusd_mode(unsol);
		return true;
	}
	if (strncmp("+CUSD:", unsol, 5) == 0) {
		orangetux_cusd_mode(unsol);
		return true;
	}
	if (strncmp("+CREG:", unsol, 5) == 0) {
//		printf(" * %s\n", unsol);

		int r = sscanf(unsol, "+CREG: %*d,%hhd,\"%hX\",\"%hX\"", &modem.mode, &modem.lap, &modem.cid);
		if (r == 0) { // initial %*d isn't counted
			r = sscanf(unsol, "+CREG: %hhd,\"%hX\",\"%hX\"", &modem.mode, &modem.lap, &modem.cid);
		}

//		printf("modem.mode=%d\n", modem.mode);

		switch(modem.mode) {
			case 0:
				modem.status = "No Network";
				break;
			case 1:
				modem.status = "%s (%d%% - %4hX:%4hX - %s)";
				break;
			case 2:
				modem.status = "Searching...";
				break;
			case 3:
				modem.status = "Restricted Network";
				break;
			case 5:
				modem.status = "%s (%d%% - %4hX:%4hX - %s) - ROAMING";
				break;
			case 4:
			default:
				modem.status = "Unknown Status";
				break;
		}

		return true;
	}
//	printf("UNSOL: %s\n", unsol);
	return false;
}

