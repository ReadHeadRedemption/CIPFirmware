// Simple LVGL + ILI9341 + XPT2046 display driver for testing
#include "display.h"

static const char *TAG = "DISPLAY";

/* LCD size for 2.8" ILI9341 */
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

MoveCmd_t head = {
    .target_x = 0.0f,
    .target_y = 0.0f,
    .target_z = 0.0f,
    .moveE = 0.0f,
    .center_x = 0.0f,
    .center_y = 0.0f,
    .circleDir = false,
    .feed_rate_hz = 5000};

///////////////////////////////////////////////////////////////////////////
//
//                          FILE SELECT SCREEN
//
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
//
//                          TEST SCREEN
//
///////////////////////////////////////////////////////////////////////////
static void tstbtn_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    homeMotors();
    lvgl_port_unlock();
}

static void parsebtn_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "PARSE GCODE");
    if (StartButtonSemaphore)
    {
        xSemaphoreGive(StartButtonSemaphore);
    }
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void xPos_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE +10 X");
    head.target_x += 10.0f;
    coordinated_move(&head);
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void xNeg_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE -10 X");
    head.target_x -= 10.0f;
    coordinated_move(&head);
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
    coordinated_move(&head);
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
    coordinated_move(&head);
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void zPos_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE +10 Z");
    head.target_z += 10.0f;
    coordinated_move(&head);
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

static void zNeg_event_cb(lv_event_t *e)
{
    if (!lvgl_port_lock(0))
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE -10 Z");
    head.target_z -= 10.0f;
    coordinated_move(&head);
    lv_label_set_text(g_status_label, buf);
    lvgl_port_unlock();
}

void test_screen(void)
{
    if (!lvgl_port_lock(0))
        return;

    ESP_LOGI(TAG, "Switching to test screen...");

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);

    /* Set a standard antialiased font. Removed subpx fonts as they look terrible when rotated. */
    // #if LV_FONT_MONTSERRAT_16
    //     lv_obj_set_style_text_font(scr, &lv_font_montserrat_16, 0);
    // #elif LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, 0);
    // #endif

    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_black(), 0);

    lv_obj_t *status_label = lv_label_create(scr);
    lv_obj_set_width(status_label, LCD_H_RES - 20);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
    lv_label_set_text(status_label, "Test screen\nPress HOME to return");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);

    // Create Back Button

    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, 50, 40);
    lv_obj_align(back_btn, LV_ALIGN_CENTER, 60, -60);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "HOME");
    lv_obj_set_style_text_color(back_label, lv_color_black(), 0);
    lv_obj_add_event_cb(back_btn, home_screen_cb, LV_EVENT_CLICKED, NULL);

    // Create Parseing Gcode Button
    lv_obj_t *parse_btn = lv_btn_create(scr);
    lv_obj_set_size(parse_btn, 100, 40);
    lv_obj_align(parse_btn, LV_ALIGN_CENTER, -50, 0);
    lv_obj_t *parse_label = lv_label_create(parse_btn);
    lv_label_set_text(parse_label, "Parse \nGcode");
    lv_obj_set_style_text_color(parse_label, lv_color_black(), 0);
    lv_obj_add_event_cb(parse_btn, parsebtn_event_cb, LV_EVENT_CLICKED, NULL);

    // Create Test Button
    lv_obj_t *tst_btn = lv_btn_create(scr);
    lv_obj_set_size(tst_btn, 100, 40);
    lv_obj_align(tst_btn, LV_ALIGN_CENTER, 50, 0);
    lv_obj_t *tst_label = lv_label_create(tst_btn);
    lv_label_set_text(tst_label, "TEST \nHOMING");
    lv_obj_set_style_text_color(tst_label, lv_color_black(), 0);
    lv_obj_add_event_cb(tst_btn, tstbtn_event_cb, LV_EVENT_CLICKED, NULL);

    // Create Increment Buttons
    lv_obj_t *posXmove = lv_btn_create(scr);
    lv_obj_set_size(posXmove, 60, 40);
    lv_obj_align(posXmove, LV_ALIGN_CENTER, -70, 60);
    lv_obj_t *posXlabel = lv_label_create(posXmove);
    lv_label_set_text(posXlabel, "MOVE \n+10X");
    lv_obj_set_style_text_color(posXlabel, lv_color_black(), 0);
    lv_obj_add_event_cb(posXmove, xPos_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *negXmove = lv_btn_create(scr);
    lv_obj_set_size(negXmove, 60, 40);
    lv_obj_align(negXmove, LV_ALIGN_CENTER, -70, 110);
    lv_obj_t *negXlabel = lv_label_create(negXmove);
    lv_label_set_text(negXlabel, "MOVE \n-10X");
    lv_obj_set_style_text_color(negXlabel, lv_color_black(), 0);
    lv_obj_add_event_cb(negXmove, xNeg_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *posYmove = lv_btn_create(scr);
    lv_obj_set_size(posYmove, 60, 40);
    lv_obj_align(posYmove, LV_ALIGN_CENTER, 0, 60);
    lv_obj_t *posYlabel = lv_label_create(posYmove);
    lv_label_set_text(posYlabel, "MOVE \n+10Y");
    lv_obj_set_style_text_color(posYlabel, lv_color_black(), 0);
    lv_obj_add_event_cb(posYmove, yPos_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *negYmove = lv_btn_create(scr);
    lv_obj_set_size(negYmove, 60, 40);
    lv_obj_align(negYmove, LV_ALIGN_CENTER, 0, 110);
    lv_obj_t *negYlabel = lv_label_create(negYmove);
    lv_label_set_text(negYlabel, "MOVE \n-10Y");
    lv_obj_set_style_text_color(negYlabel, lv_color_black(), 0);
    lv_obj_add_event_cb(negYmove, yNeg_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *posZmove = lv_btn_create(scr);
    lv_obj_set_size(posZmove, 60, 40);
    lv_obj_align(posZmove, LV_ALIGN_CENTER, 70, 60);
    lv_obj_t *posZlabel = lv_label_create(posZmove);
    lv_label_set_text(posZlabel, "MOVE \n+10Z");
    lv_obj_set_style_text_color(posZlabel, lv_color_black(), 0);
    lv_obj_add_event_cb(posZmove, zPos_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *negZmove = lv_btn_create(scr);
    lv_obj_set_size(negZmove, 60, 40);
    lv_obj_align(negZmove, LV_ALIGN_CENTER, 70, 110);
    lv_obj_t *negZlabel = lv_label_create(negZmove);
    lv_label_set_text(negZlabel, "MOVE \n-10Z");
    lv_obj_set_style_text_color(negZlabel, lv_color_black(), 0);
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
//                          DISPLAY INITILIZATION
//
///////////////////////////////////////////////////////////////////////////

static void tstscr(lv_event_t *e)
{
    test_screen();
}

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    /* SPI bus for LCD and touch */
    const spi_bus_config_t buscfg = {
        .sclk_io_num = SCK,
        .mosi_io_num = MOSI,
        .miso_io_num = MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Install panel IO (SPI) */
    const esp_lcd_panel_io_spi_config_t io_config = ILI9341_PANEL_IO_SPI_CONFIG(csDisplay, dcDisplay, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &lcd_io));

    /* Create ILI9341 panel */
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = reset, // Use GPIO_NUM_9 for proper hardware reset
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

    /* Touch (XPT2046) using same SPI bus but separate CS */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_spi_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(csTouch);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = reset,
        .int_gpio_num = touchInterrupt,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 1},
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &touch_handle));

    /* Initialize LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* Add display to LVGL */
    lvgl_port_display_cfg_t disp_cfg = {0};
    disp_cfg.io_handle = lcd_io;
    disp_cfg.panel_handle = lcd_panel;
    disp_cfg.buffer_size = LCD_H_RES * DRAW_BUFF_HEIGHT;
    /* Use double buffering to reduce flicker and improve text rendering */
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
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    /* Add touch to LVGL */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = g_lv_disp,
        .handle = touch_handle,
    };
    g_lv_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!g_lv_touch_indev)
    {
        ESP_LOGE(TAG, "Failed to add LVGL touch input");
        return;
    }

    /* Build a simple UI */
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    g_main_screen = scr;

    /* Set a standard antialiased font. Removed subpx fonts as they look terrible when rotated. */
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_16, 0);
#elif LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, 0);
#endif

    // CHANGED: Set background to white and base text to black
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_black(), 0);

    /* Status label */
    g_status_label = lv_label_create(scr);
    lv_obj_set_width(g_status_label, LCD_H_RES - 20);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0);

    // CHANGED: Status text color to black
    lv_obj_set_style_text_color(g_status_label, lv_color_black(), 0);
    lv_label_set_text(g_status_label, "Display initialized\nTouch the button to test");
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 10);

    ///////////////////////////////////////////////////////////////////////////
    //
    //                          CREATEING UI ELEMENTS
    //
    ///////////////////////////////////////////////////////////////////////////

    /* Test button */
    lv_obj_t *tstbtn = lv_btn_create(scr);
    lv_obj_set_size(tstbtn, 140, 50);
    lv_obj_align(tstbtn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_t *tstbtnlabel = lv_label_create(tstbtn);
    lv_label_set_text(tstbtnlabel, "OPEN TEST BUTTON");

    // CHANGED: Button text color to black
    lv_obj_set_style_text_color(tstbtnlabel, lv_color_black(), 0);

    /* simple event handler: increment counter and update status */
    lv_obj_add_event_cb(tstbtn, tstscr, LV_EVENT_CLICKED, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Display initialized successfully");
}

void display_task(void *pvParameters)
{
    display_init();

    /* LVGL port creates its own internal task for rendering.
       This task just needs to stay alive to keep display running. */
    // while (1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
    vTaskDelete(NULL);
}