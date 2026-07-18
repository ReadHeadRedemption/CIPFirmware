// Simple LVGL + ILI9341 + XPT2046 display driver for testing
#include "display.h"
#include "SD.h"
#include "IOExpander.h"

static const char *DISPLAYTAG = "DISPLAY";

//  LCD size for 2.8" ILI9341
#define LCD_H_RES 240
#define LCD_V_RES 320
#define DRAW_BUFF_HEIGHT 80
#define DRAW_BUFF_DOUBLE 1

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

static lv_display_t *g_lv_disp = NULL;
static lv_indev_t *g_lv_touch_indev = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_main_screen = NULL;
static lv_obj_t *testScreen = NULL;
static lv_obj_t *printScreen = NULL;
static lv_obj_t *printFinishedScreen = NULL;
static lv_obj_t *loadingScreen = NULL;

static void home_screen_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    if (g_main_screen)
    {
        lv_scr_load(g_main_screen);
    }
    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          AFTER PRINT SCREEN
//
///////////////////////////////////////////////////////////////////////////
lv_obj_t *reflow_temp = NULL;

static void reflow_parts_cb(lv_event_t *e)
{
    vTaskSuspend(parserHandle); // Stop the parser task to prevent any new commands from being processed during reflow
    printf("Reflowing parts...\n");
    parse("G1 E-400 F200"); // large pull out to stop ink extrusion
    parse("G0 X0 Y-20 Z150");
    parse("M84");
    parse("M190 S140");
    parse("G4 S45"); // Sleep for a while, seconds
    parse("M190 S190");
    parse("G4 S30"); // Sleep for a while, seconds
    parse("M190 S0");
    parse("G4 S1800"); // cool-down wait 30 min
}

void reflowTempChange(float temp)
{
    if (!lvgl_port_lock(0))
        return;
    int temp_int = (int)temp;
    int temp_frac = (int)((temp - temp_int) * 1000);
    lv_label_set_text_fmt(reflow_temp, "Temperature: %d.%03d °C", temp_int, temp_frac);

    lvgl_port_unlock();
}

void show_print_finished_screen()
{
    if (!lvgl_port_lock(0))
        return;

    ESP_LOGI(DISPLAYTAG, "Creating print finished screen...");

    // ====================================================
    // MEMORY MANAGEMENT: Clean old screen if it exists
    // ====================================================
    if (printFinishedScreen == NULL)
    {
        printFinishedScreen = lv_obj_create(NULL);
        ESP_LOGI(DISPLAYTAG, "Created new screen object");
    }
    else
    {
        lv_obj_clean(printFinishedScreen);
        ESP_LOGI(DISPLAYTAG, "Cleaned existing screen");
    }

    lv_obj_set_style_text_font(printFinishedScreen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(printFinishedScreen, lv_color_white(), 0);
    lv_obj_set_style_text_color(printFinishedScreen, lv_color_black(), 0);

    // ====================================================
    // 1. HEADER
    // ====================================================
    lv_obj_t *header_label = lv_label_create(printFinishedScreen);
    lv_label_set_text(header_label, "Print Finished");
    lv_obj_align(header_label, LV_ALIGN_TOP_LEFT, 10, 15);

    // Back Button
    lv_obj_t *back_btn = lv_btn_create(printFinishedScreen);
    lv_obj_set_size(back_btn, 65, 35);
    lv_obj_align(back_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "BACK");
    lv_obj_set_style_text_color(back_label, lv_color_black(), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, home_screen_cb, LV_EVENT_CLICKED, NULL);

    // ====================================================
    // 2. STATUS MESSAGE
    // ====================================================
    lv_obj_t *status_label = lv_label_create(printFinishedScreen);
    lv_obj_set_width(status_label, 220);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label, "Your print is complete!");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 60);

    // ====================================================
    // 3. REFLOW PARTS BUTTON
    // ====================================================
    lv_obj_t *reflow_btn = lv_btn_create(printFinishedScreen);
    lv_obj_set_size(reflow_btn, 160, 60);
    lv_obj_align(reflow_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(reflow_btn, lv_palette_main(LV_PALETTE_BLUE), 0);

    lv_obj_t *reflow_label = lv_label_create(reflow_btn);
    lv_label_set_text(reflow_label, "REFLOW PARTS");
    lv_obj_set_style_text_color(reflow_label, lv_color_white(), 0);
    lv_obj_center(reflow_label);
    lv_obj_add_event_cb(reflow_btn, reflow_parts_cb, LV_EVENT_CLICKED, NULL);

    reflow_temp = lv_label_create(printFinishedScreen);
    lv_obj_set_width(reflow_temp, 220);
    lv_obj_set_style_text_align(reflow_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(reflow_temp, "Temperature: --- °C");
    lv_obj_align(reflow_temp, LV_ALIGN_TOP_MID, 0, 190);
    setTempCallback(reflowTempChange);

    ESP_LOGI(DISPLAYTAG, "Loading print finished screen...");
    lv_scr_load(printFinishedScreen);
    ESP_LOGI(DISPLAYTAG, "Print finished screen loaded successfully");

    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          ACTIVE PRINT SCREEN
//
///////////////////////////////////////////////////////////////////////////

// Global references for the active print screen to prevent memory leaks
lv_obj_t *activePrintScreen = NULL;
lv_obj_t *print_status_label = NULL;
lv_obj_t *tool_head_label = NULL;
lv_obj_t *layer_info_label = NULL;
lv_obj_t *temp_info_label = NULL;

// Callback for the STOP button
static void stop_print_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;

    vTaskSuspend(parserHandle);
    // head.moveE -= 200.0f;
    // head.feed_rate_hz = 500;
    // coordinated_move(&head);
    parse("G1 Z100 E-200 F5000"); // Move Z up to avoid collision
    if (print_status_label)
    {
        lv_label_set_text(print_status_label, "Print STOPPED");
    }
    ESP_LOGI(DISPLAYTAG, "Print Stopped by User");

    lvgl_port_unlock();
}

void onLineCountChanged(int readLines, int totalLines)
{
    // printf("Line Count Changed: %d / %d\n", readLines, totalLines);

    if ((readLines >= totalLines && totalLines > 0))
    {
        show_print_finished_screen();

        vTaskSuspend(parserHandle);
    }
    else
    {
        // Update the layer label safely
        if (!lvgl_port_lock(0))
            return;

        if (layer_info_label != NULL)
        {
            lv_label_set_text_fmt(layer_info_label, "Current Step: %d / %d", readLines, totalLines);
        }

        lvgl_port_unlock();
    }
}

void onToolHeadChanged(int headID)
{
    if (!lvgl_port_lock(0))
        return;
    lv_label_set_text_fmt(tool_head_label, "Current Tool Head: %d", headID);
    lvgl_port_unlock();
}

void onTempChange(float temp)
{
    if (!lvgl_port_lock(0))
        return;
    int temp_int = (int)temp;
    int temp_frac = (int)((temp - temp_int) * 1000);
    lv_label_set_text_fmt(temp_info_label, "Temperature: %d.%03d °C", temp_int, temp_frac);

    lvgl_port_unlock();
}

void show_active_print_screen(const char *filename)
{
    if (!lvgl_port_lock(0))
        return;

    // ====================================================
    // MEMORY MANAGEMENT: Clean old screen if it exists
    // ====================================================
    if (activePrintScreen == NULL)
    {
        activePrintScreen = lv_obj_create(NULL);
    }
    else
    {
        lv_obj_clean(activePrintScreen); // Destroys all children to free memory
    }

    lv_obj_set_style_text_font(activePrintScreen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(activePrintScreen, lv_color_white(), 0);
    lv_obj_set_style_text_color(activePrintScreen, lv_color_black(), 0);

    // ====================================================
    // 1. HEADER (Y: 5 to 40)
    // ====================================================
    lv_obj_t *header_label = lv_label_create(activePrintScreen);
    lv_label_set_text(header_label, "Active Print");
    lv_obj_align(header_label, LV_ALIGN_TOP_LEFT, 10, 15);

    // Home / Back Button (Top Right)
    lv_obj_t *home_btn = lv_btn_create(activePrintScreen);
    lv_obj_set_size(home_btn, 65, 35);
    lv_obj_align(home_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_t *home_label = lv_label_create(home_btn);
    lv_label_set_text(home_label, "BACK");
    lv_obj_set_style_text_color(home_label, lv_color_black(), 0);
    lv_obj_center(home_label);
    lv_obj_add_event_cb(home_btn, home_screen_cb, LV_EVENT_CLICKED, NULL);

    // ====================================================
    // 2. CONFIRMATION & STATUS AREA (Y: 60)
    // ====================================================
    print_status_label = lv_label_create(activePrintScreen);
    lv_obj_set_width(print_status_label, 220);
    lv_obj_set_style_text_align(print_status_label, LV_TEXT_ALIGN_CENTER, 0);
    // Display the filename that was successfully sent
    lv_label_set_text_fmt(print_status_label, "Successfully Sent:\n%s", filename);
    lv_obj_align(print_status_label, LV_ALIGN_TOP_MID, 0, 60);

    // ====================================================
    // 3. TOOL HEAD & LAYER INFO (Y: 120 to 180)
    // ====================================================
    tool_head_label = lv_label_create(activePrintScreen);
    lv_obj_set_width(tool_head_label, 220);
    lv_obj_set_style_text_align(tool_head_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(tool_head_label, "Current Tool Head: ---");
    lv_obj_align(tool_head_label, LV_ALIGN_TOP_MID, 0, 120);

    layer_info_label = lv_label_create(activePrintScreen);
    lv_obj_set_width(layer_info_label, 220);
    lv_obj_set_style_text_align(layer_info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(layer_info_label, "Current Step: ---");
    lv_obj_align(layer_info_label, LV_ALIGN_TOP_MID, 0, 160);

    temp_info_label = lv_label_create(activePrintScreen);
    lv_obj_set_width(temp_info_label, 220);
    lv_obj_set_style_text_align(temp_info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(temp_info_label, "Temperature: --- °C");
    lv_obj_align(temp_info_label, LV_ALIGN_TOP_MID, 0, 190);

    setLineCountCallback(onLineCountChanged);
    setHeadCallback(onToolHeadChanged);
    setTempCallback(onTempChange);

    // ====================================================
    // 4. STOP BUTTON (Y: 230 - Anchored to bottom)
    // ====================================================
    lv_obj_t *stop_btn = lv_btn_create(activePrintScreen);
    lv_obj_set_size(stop_btn, 160, 60); // Large and prominent
    lv_obj_align(stop_btn, LV_ALIGN_BOTTOM_MID, 0, -30);

    // Styling the STOP button to be Red with White text for clear UX
    lv_obj_set_style_bg_color(stop_btn, lv_palette_main(LV_PALETTE_RED), 0);

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP PRINT");
    lv_obj_set_style_text_color(stop_label, lv_color_white(), 0);
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop_btn, stop_print_cb, LV_EVENT_CLICKED, NULL);

    // Finally, load the screen
    lv_scr_load(activePrintScreen);

    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          FILE SELECT SCREEN
//
///////////////////////////////////////////////////////////////////////////

void sendToParse(char fileName[128])
{
    if ((strstr(fileName, ".GCO")) != NULL)
    {
        ESP_LOGI(DISPLAYTAG, "I AM SENDING TO THE QUEUE");
        xQueueSend(FileName, fileName, portMAX_DELAY);
        xSemaphoreGive(StartButtonSemaphore);
        show_active_print_screen(fileName);
    }
    else
    {
        // Add an else statement so you know EXACTLY why it failed in the logs
        ESP_LOGE(DISPLAYTAG, "Queue skipped: '%s' is missing a valid G-code extension.", fileName);
    }
}

static lv_obj_t *gcode_roller_global = NULL; // Keep reference to roller for print button

static void roller_event_cb(lv_event_t *e)
{
    lv_obj_t *roller = lv_event_get_target(e);

    if (!lvgl_port_lock(0))
        return;

    // Get the selected filename
    char selected_file[128];
    lv_roller_get_selected_str(roller, selected_file, sizeof(selected_file));

    // Print to console
    ESP_LOGI(DISPLAYTAG, "Selected file: %s", selected_file);

    lvgl_port_unlock();
}

static void print_button_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;

    // Get the selected filename from the roller
    char selected_file[128];
    lv_roller_get_selected_str(gcode_roller_global, selected_file, sizeof(selected_file));

    // Print to console
    ESP_LOGI(DISPLAYTAG, "Printing file: %s", selected_file);
    sendToParse(selected_file);

    lvgl_port_unlock();
}

static void refresh_sd_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
    {
        return;
    }

    ESP_LOGI(DISPLAYTAG, "Refreshing SD card file list on display...");

    deinitializeSD();

    esp_err_t ret = initializeSD();
    if (ret == ESP_OK)
    {
        readSD();
    }

    // 1. Prepare a buffer for the roller options
    char options_buf[1024] = "";

    // 2. Open your file
    FILE *f = fopen("/sdcard/fileList.txt", "r");

    if (f != NULL)
    {
        char line[128];
        bool first_line = true;

        // 3. Read the file line by line
        while (fgets(line, sizeof(line), f) != NULL)
        {
            // Strip any existing newline/carriage returns
            line[strcspn(line, "\r\n")] = 0;

            // Ignore empty lines
            if (strlen(line) > 0)
            {
                // If it's not the first line, add LVGL's required newline separator
                if (!first_line)
                {
                    strcat(options_buf, "\n");
                }

                // Be careful not to exceed options_buf limits
                if (strlen(options_buf) + strlen(line) < sizeof(options_buf) - 1)
                {
                    strcat(options_buf, line);
                    first_line = false;
                }
            }
        }
        fclose(f);
    }

    // Fallback just in case the file couldn't be read or is empty
    if (strlen(options_buf) == 0)
    {
        strcpy(options_buf, "No files found\nCheck SD Card");
    }

    // 4. Update the existing roller globally!
    if (gcode_roller_global != NULL)
    {
        lv_roller_set_options(gcode_roller_global, options_buf, LV_ROLLER_MODE_NORMAL);
    }
    else
    {
        ESP_LOGE(DISPLAYTAG, "Roller is NULL, cannot update!");
    }

    lvgl_port_unlock();
}

void printDisplay(void)
{
    if (!lvgl_port_lock(0))
        return;

    printScreen = lv_obj_create(NULL);
    // lv_scr_load(printScreen);

    lv_obj_set_style_text_font(printScreen, &lv_font_montserrat_14, 0);

    lv_obj_set_style_bg_color(printScreen, lv_color_white(), 0);
    lv_obj_set_style_text_color(printScreen, lv_color_black(), 0);

    // Status Label at top
    lv_obj_t *status_label = lv_label_create(printScreen);
    lv_obj_set_width(status_label, LCD_H_RES - 20);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
    lv_label_set_text(status_label, "Select a file to print\nPress HOME to return");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);

    // Create Refresh SD Button (TOP LEFT, moved down)
    lv_obj_t *refresh_btn = lv_btn_create(printScreen);
    lv_obj_set_size(refresh_btn, 80, 40);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_LEFT, 10, 65);
    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "REFRESH");
    lv_obj_set_style_text_color(refresh_label, lv_color_black(), 0);
    lv_obj_add_event_cb(refresh_btn, refresh_sd_cb, LV_EVENT_CLICKED, NULL);

    // Create Home Button (TOP RIGHT, moved down)
    lv_obj_t *home_btn = lv_btn_create(printScreen);
    lv_obj_set_size(home_btn, 60, 40);
    lv_obj_align(home_btn, LV_ALIGN_TOP_RIGHT, -10, 65);
    lv_obj_t *home_label = lv_label_create(home_btn);
    lv_label_set_text(home_label, "HOME");
    lv_obj_set_style_text_color(home_label, lv_color_black(), 0);
    lv_obj_add_event_cb(home_btn, home_screen_cb, LV_EVENT_CLICKED, NULL);

    // CREATE GCODE FILES ROLLER (FULL WIDTH)
    gcode_roller_global = lv_roller_create(printScreen);

    // 1. Prepare a buffer for the roller options.
    char options_buf[1024] = "";

    // 2. Open your file
    printf("I AM OPENING THE FILE");
    FILE *f = fopen("/sdcard/fileList.txt", "r");

    if (f != NULL)
    {
        char line[128];
        bool first_line = true;

        // 3. Read the file line by line
        while (fgets(line, sizeof(line), f) != NULL)
        {
            // Strip any existing newline/carriage returns
            line[strcspn(line, "\r\n")] = 0;

            // Ignore empty lines
            if (strlen(line) > 0)
            {
                // If it's not the first line, add LVGL's required newline separator
                if (!first_line)
                {
                    strcat(options_buf, "\n");
                }

                // Be careful not to exceed options_buf limits
                if (strlen(options_buf) + strlen(line) < sizeof(options_buf) - 1)
                {
                    strcat(options_buf, line);
                    first_line = false;
                }
            }
        }
        fclose(f);
    }

    // Fallback just in case the file couldn't be read or is empty
    if (strlen(options_buf) == 0)
    {
        strcpy(options_buf, "No files found\nCheck SD Card");
    }

    // 4. Apply the options string to the roller
    lv_roller_set_options(gcode_roller_global, options_buf, LV_ROLLER_MODE_NORMAL);

    // 5. Style and position the roller (FULL WIDTH)
    lv_obj_set_width(gcode_roller_global, LCD_H_RES - 20);   // Full width with 10px margins
    lv_roller_set_visible_row_count(gcode_roller_global, 4); // Show 4 items at a time
    lv_obj_align(gcode_roller_global, LV_ALIGN_CENTER, 0, 20);

    // Add event callback to trigger when file is selected
    lv_obj_add_event_cb(gcode_roller_global, roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Create Print Button at bottom
    lv_obj_t *print_btn = lv_btn_create(printScreen);
    lv_obj_set_size(print_btn, 100, 45);
    lv_obj_align(print_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *print_label = lv_label_create(print_btn);
    lv_label_set_text(print_label, "PRINT");
    lv_obj_set_style_text_color(print_label, lv_color_black(), 0);
    lv_obj_add_event_cb(print_btn, print_button_cb, LV_EVENT_CLICKED, NULL);

    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                        TEST SCREEN
//
///////////////////////////////////////////////////////////////////////////

static void tstbtn_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    homeMotors();
    lvgl_port_unlock();
}

static void purge_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;

    parse("G0 Z60 F5000");
    parse("G1 E40 F200");
    for (int i = 0; i < 10; i++)
    {
        parse("G1 E40 F200");
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    lvgl_port_unlock();
}

static void parsebtn_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    // readHeadState();
    //  checkHead(e);
    lv_obj_t *headID = (lv_obj_t *)lv_event_get_user_data(e);
    // Newline restored for narrow 240px width
    lv_label_set_text_fmt(headID, "Current Head ID:\n %d", readHeadState());
    printf("Checking Head ID\n");
    lvgl_port_unlock();
}

static void xPos_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE +10 X");
    head.target_x += 10.0f;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(xEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    coordinated_move(&head);
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(xEnable, 1);
        xSemaphoreGive(i2c_mutex);
    }
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void xNeg_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE -10 X");
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(xEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    head.target_x -= 10.0f;
    coordinated_move(&head);
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(xEnable, 1);
        xSemaphoreGive(i2c_mutex);
    }
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void yPos_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE +10 Y");
    head.target_y += 10.0f;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(yEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    coordinated_move(&head);
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(yEnable, 1);
        xSemaphoreGive(i2c_mutex);
    }
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void yNeg_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE -10 Y");
    head.target_y -= 10.0f;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(yEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    coordinated_move(&head);
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(yEnable, 1);
        xSemaphoreGive(i2c_mutex);
    }
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void zPos_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE +10 Z");
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(zEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    head.target_z += 10.0f;
    coordinated_move(&head);
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(zEnable, 1);
        xSemaphoreGive(i2c_mutex);
    }
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void zNeg_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE -10 Z");
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(zEnable, 0);
        xSemaphoreGive(i2c_mutex);
    }
    head.target_z -= 10.0f;
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
    {
        gpio_set_level(zEnable, 1);
        xSemaphoreGive(i2c_mutex);
    }
    coordinated_move(&head);
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

void test_screen(void)
{
    if (!lvgl_port_lock(0))
        return;

    testScreen = lv_obj_create(NULL);
    // lv_scr_load(testScreen);

    // Set a standard antialiased font.
    lv_obj_set_style_text_font(testScreen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(testScreen, lv_color_white(), 0);
    lv_obj_set_style_text_color(testScreen, lv_color_black(), 0);

    // ====================================================
    // 1. HEADER AREA (Y: 5)
    // ====================================================

    // Status Label (Top Left)
    lv_obj_t *status_label = lv_label_create(testScreen);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
    lv_label_set_text(status_label, "Manual Control Screen");
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 5, 15);

    // Back Button (Top Right)
    lv_obj_t *back_btn = lv_btn_create(testScreen);
    lv_obj_set_size(back_btn, 65, 35);
    lv_obj_align(back_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "BACK");
    lv_obj_set_style_text_color(back_label, lv_color_black(), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, home_screen_cb, LV_EVENT_CLICKED, NULL);

    // ====================================================
    // 2. INFO AREA (Y: 45)
    // ====================================================

    // Head ID Label
    lv_obj_t *headID = lv_label_create(testScreen);
    lv_obj_set_width(headID, 220); // Constrained to 240px screen width
    lv_obj_set_style_text_align(headID, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(headID, lv_color_black(), 0);
    lv_label_set_text_fmt(headID, "Current Head ID:\n%d", readHeadState());
    lv_obj_align(headID, LV_ALIGN_TOP_MID, 0, 45);

    // ====================================================
    // 3. ACTION BUTTONS (Y: 90 to 185)
    // ====================================================

    int action_btn_w = 105; // 105 * 2 = 210 (Fits well inside 240px)
    int action_btn_h = 45;

    // Check Head Button
    lv_obj_t *parse_btn = lv_btn_create(testScreen);
    lv_obj_set_size(parse_btn, action_btn_w, action_btn_h);
    lv_obj_align(parse_btn, LV_ALIGN_TOP_MID, -55, 90);
    lv_obj_t *parse_label = lv_label_create(parse_btn);
    lv_label_set_text(parse_label, "CHECK\nHEAD");
    lv_obj_set_style_text_align(parse_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(parse_label, lv_color_black(), 0);
    lv_obj_center(parse_label);
    lv_obj_add_event_cb(parse_btn, parsebtn_event_cb, LV_EVENT_CLICKED, headID);

    // Test Homing Button
    lv_obj_t *homing_btn = lv_btn_create(testScreen);
    lv_obj_set_size(homing_btn, action_btn_w, action_btn_h);
    lv_obj_align(homing_btn, LV_ALIGN_TOP_MID, 55, 90);
    lv_obj_t *tst_label = lv_label_create(homing_btn);
    lv_label_set_text(tst_label, "HOME");
    lv_obj_set_style_text_align(tst_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(tst_label, lv_color_black(), 0);
    lv_obj_center(tst_label);
    lv_obj_add_event_cb(homing_btn, tstbtn_event_cb, LV_EVENT_CLICKED, NULL);

    // Purge Button
    lv_obj_t *purge = lv_btn_create(testScreen);
    lv_obj_set_size(purge, 215, action_btn_h); // 215px spans almost the full 240 width
    lv_obj_align(purge, LV_ALIGN_TOP_MID, 0, 145);
    lv_obj_t *purgelabel = lv_label_create(purge);
    lv_label_set_text(purgelabel, "PURGE");
    lv_obj_set_style_text_color(purgelabel, lv_color_black(), 0);
    lv_obj_center(purgelabel);
    lv_obj_add_event_cb(purge, purge_event_cb, LV_EVENT_CLICKED, NULL);

    // ====================================================
    // 4. MOVEMENT GRID (Y: 200 to 300)
    // ====================================================

    int move_btn_w = 65; // 3 x 65px = 195px (+20px total for gaps = 215px wide)
    int move_btn_h = 45;
    int gap_x = 75; // Center is 0, Left is -75, Right is +75

    // --- POSITIVE MOVES (Top Row) ---
    lv_obj_t *posXmove = lv_btn_create(testScreen);
    lv_obj_set_size(posXmove, move_btn_w, move_btn_h);
    lv_obj_align(posXmove, LV_ALIGN_TOP_MID, -gap_x, 200);
    lv_obj_t *posXlabel = lv_label_create(posXmove);
    lv_label_set_text(posXlabel, "+10 X");
    lv_obj_set_style_text_color(posXlabel, lv_color_black(), 0);
    lv_obj_center(posXlabel);
    lv_obj_add_event_cb(posXmove, xPos_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *posYmove = lv_btn_create(testScreen);
    lv_obj_set_size(posYmove, move_btn_w, move_btn_h);
    lv_obj_align(posYmove, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_t *posYlabel = lv_label_create(posYmove);
    lv_label_set_text(posYlabel, "+10 Y");
    lv_obj_set_style_text_color(posYlabel, lv_color_black(), 0);
    lv_obj_center(posYlabel);
    lv_obj_add_event_cb(posYmove, yPos_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *posZmove = lv_btn_create(testScreen);
    lv_obj_set_size(posZmove, move_btn_w, move_btn_h);
    lv_obj_align(posZmove, LV_ALIGN_TOP_MID, gap_x, 200);
    lv_obj_t *posZlabel = lv_label_create(posZmove);
    lv_label_set_text(posZlabel, "+10 Z");
    lv_obj_set_style_text_color(posZlabel, lv_color_black(), 0);
    lv_obj_center(posZlabel);
    lv_obj_add_event_cb(posZmove, zPos_event_cb, LV_EVENT_CLICKED, NULL);

    // --- NEGATIVE MOVES (Bottom Row) ---
    lv_obj_t *negXmove = lv_btn_create(testScreen);
    lv_obj_set_size(negXmove, move_btn_w, move_btn_h);
    lv_obj_align(negXmove, LV_ALIGN_TOP_MID, -gap_x, 255); // 200 + 45 (height) + 10 (gap)
    lv_obj_t *negXlabel = lv_label_create(negXmove);
    lv_label_set_text(negXlabel, "-10 X");
    lv_obj_set_style_text_color(negXlabel, lv_color_black(), 0);
    lv_obj_center(negXlabel);
    lv_obj_add_event_cb(negXmove, xNeg_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *negYmove = lv_btn_create(testScreen);
    lv_obj_set_size(negYmove, move_btn_w, move_btn_h);
    lv_obj_align(negYmove, LV_ALIGN_TOP_MID, 0, 255);
    lv_obj_t *negYlabel = lv_label_create(negYmove);
    lv_label_set_text(negYlabel, "-10 Y");
    lv_obj_set_style_text_color(negYlabel, lv_color_black(), 0);
    lv_obj_center(negYlabel);
    lv_obj_add_event_cb(negYmove, yNeg_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *negZmove = lv_btn_create(testScreen);
    lv_obj_set_size(negZmove, move_btn_w, move_btn_h);
    lv_obj_align(negZmove, LV_ALIGN_TOP_MID, gap_x, 255);
    lv_obj_t *negZlabel = lv_label_create(negZmove);
    lv_label_set_text(negZlabel, "-10 Z");
    lv_obj_set_style_text_color(negZlabel, lv_color_black(), 0);
    lv_obj_center(negZlabel);
    lv_obj_add_event_cb(negZmove, zNeg_event_cb, LV_EVENT_CLICKED, NULL);

    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          HOME SCREEN BUTTONS
//
///////////////////////////////////////////////////////////////////////////

void display_update_status(const char *text)
{
    if (!text || !g_status_label)
        return;
    if (!lvgl_port_lock(0))
        return;
    lv_label_set_text(g_status_label, text);
    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          HOME SCREEN
//
///////////////////////////////////////////////////////////////////////////

static void switch_to_ctrl_screen(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;

    ESP_LOGI(DISPLAYTAG, "Switching to test screen...");
    if (testScreen)
    {
        lv_scr_load(testScreen);
    }
    else
    {
        printf("yeah it's null");
    }
    lvgl_port_unlock();
}

static void prntscr(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;

    ESP_LOGI(DISPLAYTAG, "Switching to print screen...");
    if (printScreen)
    {
        lv_scr_load(printScreen);
    }
    else
    {
        printf("yeah it's null");
    }
    lvgl_port_unlock();
}

static void shutdown(lv_event_t *e)
{
    // Turn off the display using the ESP-IDF esp_lcd driver
    esp_lcd_panel_disp_on_off(lcd_panel, false);

    // Pause LVGL tasks to stop drawing and save CPU cycles
    lvgl_port_lock(-1);
    
    parse("M118 SHUTDOWN\n");
}

void home_screen(void)
{
    if (!lvgl_port_lock(-1))
        return;
    lv_obj_t *scr = lv_obj_create(NULL);
    g_main_screen = scr;

    // Set a standard antialiased font. Removed subpx fonts as they look terrible when rotated.
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_16, 0);
#elif LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, 0);
#endif

    // CHANGED: Set background to white and base text to black
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_black(), 0);

    // Status label
    g_status_label = lv_label_create(scr);
    lv_obj_set_width(g_status_label, LCD_H_RES - 20);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0);

    // CHANGED: Status text color to black
    lv_obj_set_style_text_color(g_status_label, lv_color_black(), 0);
    lv_label_set_text(g_status_label, "CIP Firmware initialized\nSelect from Choices Below");
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 10);

    ///////////////////////////////////////////////////////////////////////////
    //
    //                          CREATEING UI ELEMENTS
    //
    ///////////////////////////////////////////////////////////////////////////

    // Manual Control button
    lv_obj_t *ctrl_btn = lv_btn_create(scr);
    lv_obj_set_size(ctrl_btn, 140, 50);
    lv_obj_align(ctrl_btn, LV_ALIGN_CENTER, 0, -80);
    lv_obj_t *ctrl_btn_label = lv_label_create(ctrl_btn);
    lv_label_set_text(ctrl_btn_label, "CONTROL");

    // CHANGED: Button text color to black
    lv_obj_set_style_text_color(ctrl_btn_label, lv_color_black(), 0);

    // Create test screen
    // test_screen();

    // simple event handler: increment counter and update status
    lv_obj_add_event_cb(ctrl_btn, switch_to_ctrl_screen, LV_EVENT_CLICKED, NULL);

    // PRINT FILES BUTTON
    lv_obj_t *prntbtn = lv_btn_create(scr);
    lv_obj_set_size(prntbtn, 140, 50);
    lv_obj_align(prntbtn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *prntbtnlabel = lv_label_create(prntbtn);
    lv_label_set_text(prntbtnlabel, "PRINT");

    lv_obj_set_style_text_color(prntbtnlabel, lv_color_black(), 0);

    // // Create print screen
    // printDisplay();

    lv_obj_add_event_cb(prntbtn, prntscr, LV_EVENT_CLICKED, NULL);
    
    // Shutdown button
    lv_obj_t *shutdown_btn = lv_btn_create(scr);
    lv_obj_set_size(shutdown_btn, 140, 50);
    lv_obj_align(shutdown_btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_t *shutdown_btn_label = lv_label_create(shutdown_btn);
    lv_label_set_text(shutdown_btn_label, "SHUTDOWN");
    lv_obj_set_style_bg_color(shutdown_btn, lv_color_hex(0xFF0000), LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(shutdown_btn, lv_color_black(), 0);

    // // Create print screen
    // printDisplay();

    lv_obj_add_event_cb(shutdown_btn, shutdown, LV_EVENT_CLICKED, NULL);

    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          Loading screen
//
///////////////////////////////////////////////////////////////////////////

void show_loading_screen(void)
{
    if (!lvgl_port_lock(-1))
        return;

    if (loadingScreen == NULL)
    {
        loadingScreen = lv_obj_create(NULL);
    }
    else
    {
        lv_obj_clean(loadingScreen);
    }

    lv_obj_set_style_bg_color(loadingScreen, lv_color_white(), 0);

    // ====================================================
    // 1. PROJECT NAME
    // ====================================================
    lv_obj_t *project_name_label = lv_label_create(loadingScreen);
    lv_label_set_text(project_name_label, "CONDUCTIVE INK PRINTER");

    lv_obj_set_style_text_font(project_name_label, &lv_font_montserrat_14, 0);
    // FIX: Set both width AND height, then refresh
    lv_obj_set_width(project_name_label, 220);
    lv_obj_set_height(project_name_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(project_name_label, LV_LABEL_LONG_WRAP);
    lv_obj_refr_size(project_name_label); // Force size recalculation

    lv_obj_set_style_text_color(project_name_label, lv_color_black(), 0);
    lv_obj_align(project_name_label, LV_ALIGN_CENTER, 0, -50);

    // ====================================================
    // 2. LOADING SPINNER
    // ====================================================
    lv_obj_t *spinner = lv_spinner_create(loadingScreen, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 10);

    lv_obj_set_style_arc_color(spinner, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);

    // ====================================================
    // 3. CREDITS
    // ====================================================
    lv_obj_t *credits_label = lv_label_create(loadingScreen);
    lv_label_set_text(credits_label, "Firmware v1.0.0 | Created by GROUP 7");
    lv_obj_set_style_text_font(credits_label, &lv_font_montserrat_14, 0);

    // FIX: Set both width AND height, then refresh
    lv_obj_set_width(credits_label, 220);
    lv_obj_set_height(credits_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(credits_label, LV_LABEL_LONG_WRAP);
    lv_obj_refr_size(credits_label); // Force size recalculation

    lv_obj_set_style_text_color(credits_label, lv_color_black(), 0);
    lv_obj_align(credits_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    lv_scr_load(loadingScreen);
    lvgl_port_unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//                          Display Initliazation
//
///////////////////////////////////////////////////////////////////////////

void display_init(void)
{
    ESP_LOGI(DISPLAYTAG, "Initializing display...");

    const esp_lcd_panel_io_spi_config_t io_config = ILI9341_PANEL_IO_SPI_CONFIG(csDisplay, dcDisplay, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &lcd_io));

    // Create ILI9341 panel
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = reset,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_endian = LCD_RGB_ENDIAN_BGR, // CHANGED: Fixes swapped Red/Blue colors
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // CHANGED: Fixes swapped Red/Blue colors
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
#endif
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(lcd_io, &panel_config, &lcd_panel));

    esp_lcd_panel_reset(lcd_panel);
    vTaskDelay(pdMS_TO_TICKS(100)); // Give display time to stabilize after reset
    esp_lcd_panel_init(lcd_panel);
    vTaskDelay(pdMS_TO_TICKS(100)); // Ensure initialization is complete
    esp_lcd_panel_mirror(lcd_panel, true, false);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    // Touch (XPT2046) using same SPI bus but separate CS
    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_spi_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(csTouch);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = reset,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 1},
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &touch_handle));

    // Initialize LVGL port
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Add display to LVGL
    lvgl_port_display_cfg_t disp_cfg = {0};
    disp_cfg.io_handle = lcd_io;
    disp_cfg.panel_handle = lcd_panel;
    disp_cfg.buffer_size = LCD_H_RES * DRAW_BUFF_HEIGHT;
    // Use double buffering to reduce flicker and improve text rendering
    disp_cfg.double_buffer = 1;
    disp_cfg.hres = LCD_H_RES;
    disp_cfg.vres = LCD_V_RES;
    disp_cfg.monochrome = false;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
#endif
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = true;
    disp_cfg.rotation.mirror_y = false;
    disp_cfg.flags.buff_dma = 1;
    disp_cfg.flags.buff_spiram = 0;
    disp_cfg.flags.sw_rotate = 0;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.flags.swap_bytes = 0; // CHANGED: 0 fixes "crunchy text" by keeping antialiasing gradients intact
#endif
    disp_cfg.flags.full_refresh = 0; // Partial buffer refresh (more memory efficient)
    disp_cfg.flags.direct_mode = 0;

    g_lv_disp = lvgl_port_add_disp(&disp_cfg);
    if (!g_lv_disp)
    {
        ESP_LOGE(DISPLAYTAG, "Failed to add LVGL display");
        return;
    }

    // Add touch to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = g_lv_disp,
        .handle = touch_handle,
    };
    g_lv_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!g_lv_touch_indev)
    {
        ESP_LOGE(DISPLAYTAG, "Failed to add LVGL touch input");
        return;
    }
    ESP_LOGI(DISPLAYTAG, "Display initialized successfully");

    ESP_LOGI(DISPLAYTAG, "Touch handle after init: %p", touch_handle);
    if (!touch_handle)
    {
        ESP_LOGE(DISPLAYTAG, "Touch controller failed to initialize!");
    }
    else
    {
        ESP_LOGI(DISPLAYTAG, "Touch controller initialized successfully");
    }
}

void display_task(void *pvParameters)
{

    display_init();

    // 2. Add a tiny delay to ensure the LVGL timer task has synchronized
    // with the touch controller hardware
    vTaskDelay(pdMS_TO_TICKS(500));

    // 1. Initial boot: show loading screen
    show_loading_screen();

    // 2. Build ALL screens in memory now so they are ready for transitions
    // Note: Since you use lv_obj_create(NULL), these are just background
    // containers and won't overlap until you call lv_scr_load().
    home_screen();  // Builds g_main_screen
    test_screen();  // Builds testScreen
    printDisplay(); // Builds printScreen

    // 3. Optional: Move back to home screen after initialization
    // (If you don't do this, you might be left on the last screen you built)
    // if (!lvgl_port_lock(-1))
    //     return;
    // lv_scr_load(loadingScreen);
    // lvgl_port_unlock();
    // vTaskDelay(pdMS_TO_TICKS(7000)); // Show loading screen for 2 seconds
    if (!lvgl_port_lock(-1))
        return;
    lv_scr_load(g_main_screen);
    lvgl_port_unlock();
    // 4. Stay alive
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}