#ifndef SD_H
#define SD_H
#include "driver/sdmmc_types.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_types.h"
#include "esp_vfs_fat.h"
#include "dirent.h"
#include "common.h"

esp_err_t initializeSD();
void readSD(void *pvParameters);

#endif