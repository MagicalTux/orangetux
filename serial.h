bool orangetux_serial_check(void * data);
bool orangetux_serial_open();
char *orangetux_serial_cmd(const char *str, int timeout, bool malloc);
void orangetux_serial_reset();

