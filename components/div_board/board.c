#include "div_board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "led_strip.h"
static const char *TAG = "board";
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t buttons;
static led_strip_handle_t leds;
#ifdef CONFIG_DIV_GPIO2_BATTERY
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t adc_cal;
#endif
bool div_board_external_gpio(int pin, bool output)
{
    if (pin == -1) return output;
    if (!GPIO_IS_VALID_GPIO(pin) || (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin))) return false;
    return pin == 38 || pin == 39 || pin == 40 || pin == 41 || pin == 42 || pin == 45 || pin == 46;
}
esp_err_t div_board_init(void)
{
    const int cs[] = {DIV_LCD_CS, DIV_TOUCH_CS, DIV_SD_CS, DIV_CC_CS, DIV_NRF1_CS, DIV_NRF2_CS};
    for (unsigned i = 0; i < sizeof(cs) / sizeof(cs[0]); ++i) {
        gpio_set_level(cs[i], 1);
        gpio_set_direction(cs[i], GPIO_MODE_OUTPUT);
    }
    gpio_set_level(DIV_NRF1_CE, 0);
    gpio_set_direction(DIV_NRF1_CE, GPIO_MODE_OUTPUT);
    gpio_set_level(DIV_NRF2_CE, 0);
    gpio_set_direction(DIV_NRF2_CE, GPIO_MODE_OUTPUT);
#ifdef CONFIG_DIV_NRF3_ENABLED
    gpio_set_level(DIV_NRF3_CE, 0);
    gpio_set_direction(DIV_NRF3_CE, GPIO_MODE_OUTPUT);
    gpio_set_level(DIV_NRF3_CS, 1);
    gpio_set_direction(DIV_NRF3_CS, GPIO_MODE_OUTPUT);
#else
    gpio_set_level(DIV_IR_TX, 0);
    gpio_set_direction(DIV_IR_TX, GPIO_MODE_OUTPUT);
    gpio_set_direction(DIV_IR_RX, GPIO_MODE_INPUT);
#endif
    spi_bus_config_t lcd = {.mosi_io_num = DIV_LCD_MOSI, .miso_io_num = DIV_LCD_MISO,
        .sclk_io_num = DIV_LCD_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = DIV_LCD_WIDTH * 32 * 2};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(DIV_LCD_HOST, &lcd, SPI_DMA_CH_AUTO), TAG, "LCD SPI");
    spi_bus_config_t radio = {.mosi_io_num = DIV_SPI_MOSI, .miso_io_num = DIV_SPI_MISO,
        .sclk_io_num = DIV_SPI_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(DIV_RADIO_HOST, &radio, SPI_DMA_CH_AUTO), TAG, "radio SPI");
    i2c_master_bus_config_t bus = {.i2c_port = I2C_NUM_0, .sda_io_num = DIV_I2C_SDA,
        .scl_io_num = DIV_I2C_SCL, .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &i2c_bus), TAG, "I2C");
    if (i2c_master_probe(i2c_bus, DIV_PCF_ADDR, 50) == ESP_OK) {
        i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = DIV_PCF_ADDR, .scl_speed_hz = 100000};
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &dev, &buttons), TAG, "PCF8574");
        const uint8_t inputs = 0xff;
        ESP_RETURN_ON_ERROR(i2c_master_transmit(buttons, &inputs, 1, 50), TAG, "PCF inputs");
    } else ESP_LOGW(TAG, "PCF8574 absent; use touch or console");
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {.gpio_num = DIV_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight");
    led_strip_config_t strip = {.strip_gpio_num = DIV_LED_GPIO, .max_leds = DIV_LED_COUNT,
        .led_model = LED_MODEL_WS2812, .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB};
    led_strip_rmt_config_t rmt = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000};
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &leds);
    if (err == ESP_OK) led_strip_clear(leds);
    else ESP_LOGW(TAG, "LED strip: %s", esp_err_to_name(err));
#ifdef CONFIG_DIV_GPIO2_BUZZER
    gpio_set_level(DIV_BATTERY_BUZZER, 0);
    gpio_set_direction(DIV_BATTERY_BUZZER, GPIO_MODE_OUTPUT);
#elif defined(CONFIG_DIV_GPIO2_BATTERY)
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &adc), TAG, "battery ADC");
    adc_oneshot_chan_cfg_t cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc, ADC_CHANNEL_1, &cfg), TAG, "battery ADC pin");
    adc_cali_curve_fitting_config_t cal = {.unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_1,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cal, &adc_cal), TAG, "ADC calibration");
#else
    gpio_set_direction(DIV_BATTERY_BUZZER, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DIV_BATTERY_BUZZER, GPIO_FLOATING);
#endif
    return ESP_OK;
}
i2c_master_bus_handle_t div_board_i2c(void) { return i2c_bus; }
esp_err_t div_board_buttons(uint8_t *pressed)
{
    if (!pressed) return ESP_ERR_INVALID_ARG;
    *pressed = 0;
    if (!buttons) return ESP_ERR_NOT_FOUND;
    uint8_t raw;
    esp_err_t err = i2c_master_receive(buttons, &raw, 1, 20);
    if (err == ESP_OK) *pressed = (uint8_t)~raw & 0xfc;
    return err;
}
esp_err_t div_board_brightness(uint8_t value)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, value), TAG, "brightness");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
esp_err_t div_board_led(uint8_t r, uint8_t g, uint8_t b)
{
    if (!leds) return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < DIV_LED_COUNT; ++i)
        ESP_RETURN_ON_ERROR(led_strip_set_pixel(leds, i, r, g, b), TAG, "LED pixel");
    return led_strip_refresh(leds);
}
esp_err_t div_board_battery_mv(int *millivolts)
{
    if (!millivolts) return ESP_ERR_INVALID_ARG;
#ifdef CONFIG_DIV_GPIO2_BATTERY
    int raw, mv;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(adc, ADC_CHANNEL_1, &raw), TAG, "ADC read");
    ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(adc_cal, raw, &mv), TAG, "ADC scale");
    *millivolts = mv * 2; /* R14 = R18 = 100k in both supplied schematics. */
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t div_board_beep(bool on)
{
#ifdef CONFIG_DIV_GPIO2_BUZZER
    return gpio_set_level(DIV_BATTERY_BUZZER, on);
#else
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
