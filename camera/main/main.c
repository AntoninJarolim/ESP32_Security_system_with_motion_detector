
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <sys/param.h>
#include <esp_system.h>

#include "lwip/err.h"
#include "lwip/sys.h"

// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"

// ESP32Cam (AiThinker) PIN Map
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 //software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

static const char *TAG = "wifi station";

#define EXAMPLE_ESP_WIFI_SSID      "uzasna-wifi"
#define EXAMPLE_ESP_WIFI_PASS      "kokos123"
#define EXAMPLE_ESP_WIFI_CHANNEL   6
#define EXAMPLE_MAX_STA_CONN       10


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}




static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_QVGA,    

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static esp_err_t init_camera()
{
    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

size_t get_index_from_request(httpd_req_t *req){    
    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    size_t value = 0;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "at_index", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => at_index=%s", param);
            }
            value = (size_t) atoi(param);
        }
        free(buf);
    }
    return value;
}

typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

#define MAX_PHOTO_SIZE 20000

char buffer[MAX_PHOTO_SIZE];
typedef struct photo{
    void* data;
    size_t len;
} photo_t;

photo_t photos[100];
size_t current_photo = 0;

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len) {
    size_t *fb_len = (size_t *)arg;    

    if(len > 0) {
        // Another data received -> save them to buffer
        if(*fb_len + len > MAX_PHOTO_SIZE) {
            ESP_LOGI(TAG, "Podarilo se vyfotit prilis velkou fotku.");
            return 0;
        }
        *fb_len += len;
        ESP_LOGI(TAG, "Writing chunk to buffer at %zu - len %zu.", index, len);
        memcpy(&buffer[index], data, len);
    } else {
        // Ending call -> copy buffer to new photo at current_photo index
        ESP_LOGI(TAG, "Saving new photo (%zu) lenght: %zu", current_photo, *fb_len);
        photos[current_photo].data = malloc(*fb_len);
        if (photos[current_photo].data == NULL){
            ESP_LOGE(TAG, "Zivot marny!");
            return 0;
        }
        memcpy(photos[current_photo].data, buffer, *fb_len);
        photos[current_photo++].len = *fb_len; 
    }
    
    return len;
}

esp_err_t new_photo_handler(httpd_req_t *req){
    esp_err_t res = ESP_OK;
    camera_fb_t * fb = NULL;
    size_t fb_len = 0;

    // Stop feeding frame buffer and get pointer to data
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Call function converting fb to jpg -> callback function jpg_encode_stream will be called bunch of times
    res = frame2jpg_cb(fb, 90, jpg_encode_stream, &fb_len) ? ESP_OK : ESP_FAIL;    

    // Start feeding the frame buffer with new data
    esp_camera_fb_return(fb);
    char buffer[50];
    sprintf(buffer, "OK - saved photo %zu", current_photo - 1);
    httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
    return res;
}

esp_err_t send_photo_handler(httpd_req_t *req){
    esp_err_t res = ESP_OK;
    // Set headers
    res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK){
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }


    size_t photo_index = get_index_from_request(req);
    if(photo_index >= current_photo){
        ESP_LOGE(TAG, "The requested photo at index %zu does not exist yet!", current_photo);
        httpd_resp_send_500(req);
    } else {
        httpd_resp_send(req, photos[photo_index].data, photos[photo_index].len);
        ESP_LOGI(TAG, "Sending photo at index %zu!", photo_index);
    }
   return res;
}

esp_err_t bmp_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint8_t * buf = NULL;
    size_t buf_len = 0;
    bool converted = frame2bmp(fb, &buf, &buf_len);
    esp_camera_fb_return(fb);
    if(!converted){
        ESP_LOGE(TAG, "BMP conversion failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    res = httpd_resp_set_type(req, "image/x-windows-bmp")
       || httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp")
       || httpd_resp_send(req, (const char *)buf, buf_len);
    free(buf);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "BMP: %uKB %ums", (uint32_t)(buf_len/1024), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

// Structures indicating api endpoints
static const httpd_uri_t bmp = {
    .uri       = "/bmp",
    .method    = HTTP_GET,
    .handler   = bmp_httpd_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t new_photo = {
    .uri       = "/new_photo",
    .method    = HTTP_GET,
    .handler   = new_photo_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t get_photo = {
    .uri       = "/get_photo",
    .method    = HTTP_GET,
    .handler   = send_photo_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &new_photo);
        httpd_register_uri_handler(server, &get_photo);
        httpd_register_uri_handler(server, &bmp);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#define LED_BUILTIN 33
#define FLASH_LIGHT 4

void app_main(void)
{    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize witi to AP mode
    wifi_init_softap();

    // Initialize camera
    if(ESP_OK != init_camera()) {
        return;
    }
    
    // Turn the web server on
    start_webserver();

    // BLINK three times to indicate that initialization was succesfull
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 3; i++) {
        gpio_set_level(LED_BUILTIN, 0);
        vTaskDelay(100); 
        gpio_set_level(LED_BUILTIN, 1);
        vTaskDelay(100);
    }
    
}
