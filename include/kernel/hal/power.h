#ifndef POWER_H
#define POWER_H

void power_restart(void) __attribute__((noreturn));
void power_off(void) __attribute__((noreturn));
void power_halt(void) __attribute__((noreturn));

/* Ctrl+Alt+Del: restart at once, or (after reboot(CAD_OFF)) SIGINT to
 * init, as on Linux. */
void power_ctrl_alt_del(void);
void power_set_cad(int restart);

#endif /* POWER_H */
