#ifndef SD_CARD_H
#define SD_CARD_H

#define SD_CARD_TAG "[SD-card reader]"
#define SD_CARD_MOUNT_POINT "/sdcard"

#include <stdbool.h>

void init_sd_card_reader(void);
bool is_sd_card_mounted(void);

#endif
