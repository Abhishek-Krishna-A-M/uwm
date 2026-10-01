#ifndef ULAUNCH_MODE_DMENU_H
#define ULAUNCH_MODE_DMENU_H

#include <stdbool.h>

int mode_dmenu(void);
/* Returns the number of entries added by this call. The caller must only
 * re-filter when that is > 0: a poll wakeup on POLLHUP or on a partial read
 * adds nothing, and re-filtering a 36k-entry list per wakeup is what makes
 * a bulk stdin list feel like a hang. */
int dmenu_pump(void);

#endif
