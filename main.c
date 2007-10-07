#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <gtk/gtk.h>

#include "data/tux.64x64_alpha.xpm"

#include "serial.h"
#include "modem.h"
#include "main.h"

static GtkMenu *popup_menu;
static GtkStatusIcon *icon;
static GtkCheckMenuItem *menu_planemode;
static GtkCheckMenuItem *menu_connect;
static GtkDialog *orangetux_dialog;
static int ppp = 0;
static int ppp_pid = 0;

static gboolean orangetux_activate(GtkWidget *widget, GdkEvent *event, gpointer data) {
	g_print("Activate\n");
	return TRUE;
}

static gboolean orangetux_dialog_dovalid(GtkWidget *widget, GdkEvent *event, gpointer data) {
	gtk_dialog_response(orangetux_dialog, GTK_RESPONSE_ACCEPT);
	return TRUE;
}

static gboolean orangetux_main_pppd_check(gpointer data) {
	int status;
	int end_pid;
	if (ppp_pid == 0) return FALSE;
	end_pid = waitpid(ppp_pid, &status, WNOHANG);
	if (end_pid == 0) {
		// all is good TODO: read data from ppp if any
		return TRUE;
	}
	if (end_pid == -1) { // for some reason pppd disappeared
		perror("waitpid");
		ppp_pid = 0;
		close(ppp);
		ppp = 0;
		menu_connect->active = FALSE;
		return FALSE;
	}
	// got status, check it
	if (WIFEXITED(status)) { // pppd died
		ppp_pid = 0;
		close(ppp);
		ppp = 0;
		menu_connect->active = FALSE;
		return FALSE;
	}
	return TRUE;
}

static gboolean orangetux_main_disconnect() {
	int status;
	if (ppp == 0) return TRUE;
	if (kill(ppp_pid, SIGTERM) != -1) {
		int end_pid;
		for(int i=0; i<4;i++) {
			end_pid = waitpid(ppp_pid, &status, WNOHANG);
			if (end_pid != 0) break;
			sleep(1);
		}
		if (end_pid == -1) {
			kill(ppp_pid, SIGKILL); // don't let ppp stay too long after we began trying to kill it
			waitpid(ppp_pid, &status, 0); // wait.. if pppd was killed with SIGKILL it's safe to assume it got really killed
		}
	} else {
		waitpid(ppp_pid, &status, 0); // we couldn,t kill ppp_pid, so we assume it died on us, it's safe to assume that waitpid will either return directly with status info, or return directly with a ECHLD error
	}
	// at this point, we MUST have killed pppd
	ppp_pid = 0;
	close(ppp);
	ppp = 0;
	menu_connect->active = FALSE;
	return TRUE;
}

static gboolean orangetux_main_connect(GtkWidget *widget, GdkEvent *event, gpointer data) {
	int pfd[2];
	if (!menu_connect->active) {
		orangetux_main_disconnect();
		return TRUE;
	}
	orangetux_modem_set_network("orange.fr");
	if (pipe(pfd) == -1) {
		perror("pipe");
		return FALSE;
	}
	ppp_pid = fork();
	if (ppp_pid == -1) {
		perror("fork");
		close(pfd[0]);
		close(pfd[1]);
		return FALSE;
	}
	if (ppp_pid == 0) {
		// child
		dup2(pfd[1], 1); // does not set close on exec
		char *nargv[] = {
			"/usr/sbin/pppd",
			"connect",
			"/usr/sbin/chat ABORT BUSY ABORT 'NO CARRIER' ABORT ERROR '' AT OK ATDT*99# CONNECT",
			"/dev/noz0",
			"9600", // won't care about speed anyway
			"login",
			"user",
			"any",
			"password",
			"any",
			"crtscts",
			"nodetach",
			"usepeerdns",
			"defaultroute",
			NULL
		};
		execv(nargv[0], nargv);
		exit(4);
	}
	ppp = pfd[0];
	close(pfd[1]);
	g_timeout_add(200, orangetux_main_pppd_check, NULL);
	return TRUE;
}

static gboolean orangetux_start_ussd(GtkWidget *widget, GdkEvent *event, gpointer data) {
	static bool in_dialog = false;
	GtkWidget *label;
	GtkEntry *entry;

	orangetux_dialog = (GtkDialog*)gtk_dialog_new_with_buttons("OrangeTux - USSD", NULL, GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);

	label = gtk_label_new("Please enter an initial USSD query ; for example #123# ...");
	gtk_container_add((GtkContainer*)orangetux_dialog->vbox, label);
	gtk_widget_show(label);

	entry = (GtkEntry*)gtk_entry_new_with_max_length(16);
	gtk_container_add((GtkContainer*)orangetux_dialog->vbox, (GtkWidget*)entry);
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(orangetux_dialog_dovalid), NULL);
	gtk_entry_set_text(entry, "#123#");
	gtk_widget_show((GtkWidget*)entry);

	if (gtk_dialog_run(orangetux_dialog) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy((GtkWidget*)entry);
		gtk_widget_destroy((GtkWidget*)label);
		gtk_widget_destroy((GtkWidget*)orangetux_dialog);
		orangetux_modem_end_ussd();
		in_dialog = false;
		return TRUE;
	}

	orangetux_modem_send_ussd((char*)gtk_entry_get_text(entry));

	gtk_widget_destroy((GtkWidget*)entry);
	gtk_widget_destroy((GtkWidget*)label);
	gtk_widget_destroy((GtkWidget*)orangetux_dialog);
	in_dialog = false;
	return TRUE;
}

void orangetux_main_ussd_callback(int type, char *str) {
	static bool in_dialog = false;
	GtkWidget *label;
	GtkEntry *entry;

	orangetux_dialog = (GtkDialog*)gtk_dialog_new_with_buttons("OrangeTux - USSD", NULL, GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);

	label = gtk_label_new(str);
	gtk_container_add((GtkContainer*)orangetux_dialog->vbox, label);
	gtk_widget_show(label);

	if (type == 1) {
		entry = (GtkEntry*)gtk_entry_new_with_max_length(16);
		gtk_container_add((GtkContainer*)orangetux_dialog->vbox, (GtkWidget*)entry);
		g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(orangetux_dialog_dovalid), NULL);
		gtk_entry_set_text(entry, "#123#");
		gtk_widget_show((GtkWidget*)entry);
	}

	if (gtk_dialog_run(orangetux_dialog) != GTK_RESPONSE_ACCEPT) {
		if (type == 1) gtk_widget_destroy((GtkWidget*)entry);
		gtk_widget_destroy((GtkWidget*)label);
		gtk_widget_destroy((GtkWidget*)orangetux_dialog);
		orangetux_modem_end_ussd();
		in_dialog = false;
		return;
	}

	if (type == 1) {
		orangetux_modem_send_ussd((char*)gtk_entry_get_text(entry));
		gtk_widget_destroy((GtkWidget*)entry);
	} else {
		orangetux_modem_end_ussd();
	}

	gtk_widget_destroy((GtkWidget*)label);
	gtk_widget_destroy((GtkWidget*)orangetux_dialog);
	in_dialog = false;
}

void orangetux_main_set_tooltip(char *str) {
	gtk_status_icon_set_tooltip(icon, str);
}

static gboolean orangetux_main_update_status(gpointer data) {
	char query[512];
	static bool in_dialog = false;
	orangetux_modem_update_status();

	if (in_dialog) return TRUE;

	char *auth = orangetux_modem_cpin();
	if (auth == NULL) return TRUE;

	in_dialog = true;

	GtkWidget *label;
	GtkEntry *entry;

	orangetux_dialog = (GtkDialog*)gtk_dialog_new_with_buttons("OrangeTux", NULL, GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);

	sprintf((char*)&query, "Please provide the %s code to enable your modem.", auth);
	label = gtk_label_new((char*)&query);
	gtk_container_add((GtkContainer*)orangetux_dialog->vbox, label);
	gtk_widget_show(label);

	entry = (GtkEntry*)gtk_entry_new_with_max_length(16);
	gtk_entry_set_invisible_char(entry, '#');
	gtk_entry_set_visibility(entry, FALSE);
	gtk_container_add((GtkContainer*)orangetux_dialog->vbox, (GtkWidget*)entry);
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(orangetux_dialog_dovalid), NULL);
	gtk_widget_show((GtkWidget*)entry);

	if (gtk_dialog_run(orangetux_dialog) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy((GtkWidget*)entry);
		gtk_widget_destroy((GtkWidget*)label);
		gtk_widget_destroy((GtkWidget*)orangetux_dialog);
		orangetux_modem_set_planemode(true); // switch to plane mode if no code provided
		in_dialog = false;
		return TRUE;
	}

	orangetux_modem_send_cpin((char*)gtk_entry_get_text(entry));

	gtk_widget_destroy((GtkWidget*)entry);
	gtk_widget_destroy((GtkWidget*)label);
	gtk_widget_destroy((GtkWidget*)orangetux_dialog);
	in_dialog = false;
	return TRUE;
}

static gboolean orangetux_popup(GtkWidget *widget, GdkEvent *event, gpointer data) {
	gtk_menu_popup(popup_menu, NULL, NULL, gtk_status_icon_position_menu, widget, 1, gtk_get_current_event_time());

	return TRUE;
}

static gboolean orangetux_menu_planemode(GtkWidget *widget, GdkEvent *event, gpointer data) {
	orangetux_modem_set_planemode(menu_planemode->active);
	return TRUE;
}

static gboolean orangetux_quit(GtkWidget *widget, GdkEvent *event, gpointer data) {
	gtk_main_quit();
	return TRUE;
}

int main(int argc, char *argv[], char *envp[]) {
	GdkPixbuf *buf;
	GtkMenuItem *item;
	int i;

	if (getuid() != 0) {
		// TODO: implement detection of various systems similar to gksu (eg. gksudo, etc) and call the first one found
		char **nargv = calloc(argc+1, sizeof(char*));
		nargv[0] = "/usr/bin/gksu";
		for(i=0; i<argc;i++) nargv[i+1] = argv[i];
		execve(nargv[0], nargv, envp);
		perror("execve");
		fprintf(stderr, "Please run me as root!");
		return 1;
	}

#ifndef __DEBUG
	if (fork() > 0) return 0; // successfully forked
	setsid();
#endif

	gtk_init (&argc, &argv);

	popup_menu = (GtkMenu*)gtk_menu_new();

	menu_connect = (GtkCheckMenuItem*)gtk_check_menu_item_new_with_mnemonic("_Connect");
	g_signal_connect(G_OBJECT(menu_connect), "toggled", G_CALLBACK(orangetux_main_connect), NULL);
	gtk_menu_shell_append((GtkMenuShell*)popup_menu, (GtkWidget*)menu_connect);
	gtk_widget_show((GtkWidget*)menu_connect);

	item = (GtkMenuItem*)gtk_menu_item_new_with_mnemonic("Send _USSD");
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(orangetux_start_ussd), NULL);
	gtk_menu_shell_append((GtkMenuShell*)popup_menu, (GtkWidget*)item);
	gtk_widget_show((GtkWidget*)item);

	menu_planemode = (GtkCheckMenuItem*)gtk_check_menu_item_new_with_mnemonic("_Plane mode");
	g_signal_connect(G_OBJECT(menu_planemode), "toggled", G_CALLBACK(orangetux_menu_planemode), NULL);
	gtk_menu_shell_append((GtkMenuShell*)popup_menu, (GtkWidget*)menu_planemode);
	gtk_widget_show((GtkWidget*)menu_planemode);

	item = (GtkMenuItem*)gtk_menu_item_new_with_mnemonic("_Quit");
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(orangetux_quit), NULL);
	gtk_menu_shell_append((GtkMenuShell*)popup_menu, (GtkWidget*)item);
	gtk_widget_show((GtkWidget*)item);

	buf = gdk_pixbuf_new_from_xpm_data((const char **)tux_64x64_alpha_xpm);
	icon = gtk_status_icon_new_from_pixbuf(buf); // new icon object
	gtk_status_icon_set_tooltip(icon, "Please wait...");
//	gtk_status_icon_set_blinking(icon, TRUE);

	g_signal_connect (G_OBJECT (icon), "popup-menu",
			  G_CALLBACK (orangetux_popup), NULL);

	g_signal_connect (G_OBJECT (icon), "activate",
			  G_CALLBACK (orangetux_activate), NULL);

	if (!orangetux_serial_open()) return 1;
	g_timeout_add(100, (void*)orangetux_serial_check, NULL);

	orangetux_modem_init();
	g_timeout_add(3000, orangetux_main_update_status, NULL);

	if (orangetux_modem_get_planemode()) menu_planemode->active = TRUE;
	orangetux_modem_update_status();

	atexit((void*)orangetux_main_disconnect);
	// TODO: catch signals
	gtk_main();

	return 0;
}

