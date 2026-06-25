#include "SD.h"

const char *TAG = "SD CARD";

sdmmc_card_t *card;

esp_err_t initializeSD()
{
    // REMOVE JTAG PIN ASSOCIATION
    gpio_reset_pin(GPIO_NUM_39);
    gpio_reset_pin(GPIO_NUM_40);
    gpio_reset_pin(GPIO_NUM_41);
    gpio_reset_pin(GPIO_NUM_42);

    // Initialize SDMMC host and slot configuration
    sdmmc_host_init();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.cmd = SD_CMD;
    slot.clk = SD_CLK;
    slot.d0 = SD_D0;
    slot.d1 = SD_D1;
    slot.d2 = SD_D2;
    slot.d3 = SD_D3;
    // slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
    // sdmmc_host_set_bus_width(SDMMC_HOST_SLOT_1, slot.width);
    // sdmmc_host_set_card_clk(SDMMC_HOST_SLOT_1, 20000); // 20 MHz

    // Card Initilization and Mounting
    // sdmmc_card_init(&host, &card);

    // Mount the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .disk_status_check_enable = false,
        .use_one_fat = false,
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};
    esp_err_t ret;
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
            return ESP_FAIL; // Stop here! Do not proceed to print_info or read/write.
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
            esp_err_to_name(ret));
        }
    }
    return ESP_OK;
}

esp_err_t deinitializeSD()
{
    esp_vfs_fat_sdmmc_unmount();
    return ESP_OK;
}

// CREATING FILE STURCTURE VARIABLES
DIR *dir;
struct dirent *entry;

esp_err_t openDirectory()
{
    dir = opendir("/sdcard");
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open directory");
        return ESP_FAIL;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        ESP_LOGI(TAG, "FILE: %s", entry->d_name);
    }
    // closedir(dir);
    return ESP_OK;
}

void readSD(void *pvParameters)
{
    bool flag = true;
    dir = opendir("/sdcard");
    while (1)
    {
        printf("Reading SD card...\n");

        if (card != NULL)
        {
            sdmmc_card_print_info(stdout, card);
            // print out file names to check reading
            if (flag)
            {
                flag = false;
                FILE *file = fopen("/sdcard/fileList.txt", "w");
                while ((entry = readdir(dir)) != NULL)
                {
                    char *name;
                    // Search the directory for Gcode files
                    if ((name = strstr(entry->d_name, ".GCO")) != NULL)
                    {
                        printf("%s\n", name);
                        // store the of files in an array
                        fprintf(file, "%s\n", entry->d_name);
                        printf("I AM PRINTING TO THE FILE: %s\n", entry->d_name);
                    }
                }
                fclose(file);
            } 
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS); // Delay for 1 second
    }
    closedir(dir);
    vTaskDelete(NULL);
}
