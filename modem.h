
bool orangetux_modem_parse_unsol(char *unsol);
void orangetux_modem_init();
void orangetux_modem_update_status();
char *orangetux_modem_cpin();
bool orangetux_modem_send_cpin(char *cpin);
void orangetux_modem_set_planemode(bool enable);
bool orangetux_modem_get_planemode();
bool orangetux_modem_send_ussd(const char *data);
bool orangetux_modem_end_ussd();
bool orangetux_modem_set_network(char *net);

