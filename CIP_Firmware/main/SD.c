#include "SD.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

const char *TAG = "SD CARD";

sdmmc_card_t *card;
DIR *dir;
struct dirent *entry;

void readSD()
{
    dir = opendir("/sdcard");
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open directory. Is the SD card inserted?");
        return;
    }
    printf("Reading SD card...\n");
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

    closedir(dir);
}

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
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t deinitializeSD()
{
    // 1. Unmount the FAT filesystem and free the card structure
    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", card);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Unmount failed or already unmounted: %s", esp_err_to_name(ret));
    }

    // 2. De-initialize the SDMMC host controller completely
    ret = sdmmc_host_deinit();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Host deinit failed or already de-initialized: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

// CREATING FILE STURCTURE VARIABLES

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

void sdInfo()
{
    // Configure test parameters
    const size_t CHUNK_SIZE = 64 * 1024; // Read 64 KB at a time
    const size_t NUM_SECTORS = CHUNK_SIZE / 512; // 512 bytes per sector
    const int NUM_CHUNKS = 16; // 16 chunks * 64 KB = 1 Megabyte total test
    
    // CRITICAL: Memory MUST be DMA-capable for fast SDMMC transfers
    uint8_t *buf = heap_caps_malloc(CHUNK_SIZE, MALLOC_CAP_DMA);
    if (buf == NULL) 
    {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer for speed test");
        vTaskDelete(NULL); // Kill task if we can't allocate memory
        return;
    }
        // Only run the test if the card is actually initialized
        if (card != NULL) 
        {
            ESP_LOGI(TAG, "Starting 1MB Read Speed Test...");
            
            esp_err_t ret = ESP_OK;
            
            // Start microsecond timer
            int64_t start_time = esp_timer_get_time(); 
            
            // Read 1MB sequentially in 64KB chunks
            for (int i = 0; i < NUM_CHUNKS; i++) 
            {
                ret = sdmmc_read_sectors(card, buf, i * NUM_SECTORS, NUM_SECTORS);
                if (ret != ESP_OK) break;
            }
            
            // Stop microsecond timer
            int64_t end_time = esp_timer_get_time();

            if (ret == ESP_OK)
            {
                // Calculate time and speed
                float time_seconds = (end_time - start_time) / 1000000.0f;
                float size_megabytes = (CHUNK_SIZE * NUM_CHUNKS) / (1024.0f * 1024.0f);
                float speed_mb_s = size_megabytes / time_seconds;

                ESP_LOGI(TAG, "Test Complete: Read 1MB in %.3f seconds", time_seconds);
                ESP_LOGI(TAG, "Average Read Speed: %.2f MB/s", speed_mb_s);
            }
            else
            {
                ESP_LOGE(TAG, "Speed test failed during sector read: %s", esp_err_to_name(ret));
            }
        }
        else
        {
            ESP_LOGW(TAG, "Cannot test speed: SD card is not initialized.");
        }
    
    // Free the buffer if the loop ever somehow breaks (good practice)
    free(buf); 
}