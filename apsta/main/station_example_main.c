/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_vfs.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
//#include "protocol_examples_common.h"
#include "rgb_led.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#define EXAMPLE_ESP_WIFIAP_SSID CONFIG_ESP_WIFIAP_SSID
#define EXAMPLE_ESP_WIFIAP_PASS CONFIG_ESP_WIFIAP_PASSWORD
#define EXAMPLE_ESP_WIFIAP_CHANNEL CONFIG_ESP_WIFIAP_CHANNEL
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN
#define MAX_CLIENTS 7
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";
int r = 0, g = 0, b = 0;
static int s_retry_num = 0;
httpd_handle_t handle = NULL;
/* Function to initialize SPIFFS */
static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5, // This decides the maximum number of files that can be created on the storage
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}
/*
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
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
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
*/

void save_wifi_cfg(char *ssid, char *password)
{
    nvs_handle_t handle;
    esp_err_t ret;
    ret = nvs_open("WIFI_CONFIG", NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        printf("NVS_OPEN FAILED\n");
        return;
    }
    ret = nvs_set_str(handle, "ssid", ssid);
    if (ret != ESP_OK)
        printf("NVS_SAVE ssid FAILED\n");
    ret = nvs_set_str(handle, "wifi_password", password);
    if (ret != ESP_OK)
        printf("NVS_SAVE password FAILED\n");
    nvs_close(handle);
}

esp_err_t restore_wifi_cfg(char *ssid, char *password)
{
    nvs_handle_t handle;
    esp_err_t ret;
    ret = nvs_open("WIFI_CONFIG", NVS_READONLY, &handle);
    if (ret != ESP_OK)
    {
        printf("NVS_OPEN FAILED\n");
        return ret;
    }
    size_t len = 32;
    if ((ret = nvs_get_str(handle, "ssid", ssid, &len)) != ESP_OK)
    {
        nvs_close(handle);
        printf("nvs_get_str ssid failed\n");
        return ret;
    }
    len = 32;
    if ((ret = nvs_get_str(handle, "wifi_password", password, &len)) != ESP_OK)
    {
        nvs_close(handle);
        printf("nvs_get_str password failed\n");
        return ret;
    }
    nvs_close(handle);
    return ESP_OK;
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };
    char ssid[32];
    char password[32];

    if (restore_wifi_cfg((char *)ssid, (char *)password) == ESP_OK)
    {
        memcpy(wifi_config.sta.ssid, ssid, sizeof(ssid));
        memcpy(wifi_config.sta.password, password, sizeof(password));
        printf("restor wifi ssid:%s,username:%s\n", ssid, password);
    }
    else
    {
        printf("FAILED,restor wifi ssid,username\n");
    }

    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFIAP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFIAP_SSID),
            .channel = EXAMPLE_ESP_WIFIAP_CHANNEL,
            .password = EXAMPLE_ESP_WIFIAP_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };

    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); //(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    // vEventGroupDelete(s_wifi_event_group);
}

void wifi_reconnect(char *web_ssid, char *web_password)
{
    // esp_netif_t *netif=NULL;
    wifi_config_t cfg;
    esp_wifi_disconnect();
    esp_wifi_stop();

    /*
    while((netif=esp_netif_next(netif))!=NULL)
    {
        printf("%s\n",esp_netif_get_desc(netif));
        if(strcmp(esp_netif_get_desc(netif),"sta")==0)
        {
            esp_wifi_get_config(WIFI_IF_STA,&cfg);
        }
        printf("ssid=%s\n",cfg.sta.ssid);
    }
    */
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    memcpy(cfg.ap.ssid, web_ssid, strlen(web_ssid));
    memcpy(cfg.ap.password, web_password, strlen(web_password));

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
    // esp_wifi_connect();
}

#define PORT CONFIG_EXAMPLE_PORT
#define KEEPALIVE_IDLE CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT CONFIG_EXAMPLE_KEEPALIVE_COUNT

// static const char *TAG = "example";

static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[128];

    do
    {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0)
        {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        }
        else if (len == 0)
        {
            ESP_LOGW(TAG, "Connection closed");
        }
        else
        {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = len;
            while (to_write > 0)
            {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0)
                {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                to_write -= written;
            }
        }
    } while (len > 0);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET)
    {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_EXAMPLE_IPV6
    else if (addr_family == AF_INET6)
    {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1)
    {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET)
        {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_EXAMPLE_IPV6
        else if (source_addr.ss_family == PF_INET6)
        {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}
/*
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif
}
*/
esp_err_t index_handler(httpd_req_t *req)
{
    printf("%s,%d in index_handler\n", __FILE__, __LINE__);
    extern const unsigned char upload_script_start[] asm("_binary_home_html_start");
    extern const unsigned char upload_script_end[] asm("_binary_home_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);
    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
esp_err_t ico_handler(httpd_req_t *req)
{
    printf("%s,%d in ico_handler\n", __FILE__, __LINE__);
    extern const unsigned char upload_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char upload_ico_end[] asm("_binary_favicon_ico_end");
    const size_t upload_ico_size = (upload_ico_end - upload_ico_start);

    // httpd_resp_send_chunk(req,(const char *)upload_ico_start,upload_ico_size);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)upload_ico_start, upload_ico_size);
    /* Send empty chunk to signal HTTP response completion */
    // httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
esp_err_t username_handler(httpd_req_t *req)
{
    char web_set_ssid[100] = {0};
    char web_set_password[100] = {0};
    printf("USERNAME:%s,%d in username_handler\n", __FILE__, __LINE__);
    char buf[100] = {0};
    int ret, remaining = req->content_len;
    printf("remaining=%d\n", remaining);
    while (remaining > 0)
    {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                                  MIN(remaining, sizeof(buf)))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        /* Send back the same data */
        // httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;
        printf(TAG, "%.*s", ret, buf);
        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
    sscanf(buf, "%s %s", web_set_ssid, web_set_password);
    printf("web_set_ssid=%s,web_set_password=%s\n", web_set_ssid, web_set_password);
    // End response
    // httpd_resp_send_chunk(req, NULL, 0);
    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    save_wifi_cfg(web_set_ssid, web_set_password);
    wifi_reconnect(web_set_ssid, web_set_password);
    return ESP_OK;
}

esp_err_t ap_sta_info_handler(httpd_req_t *req)
{
    // char web_set_ssid[100]={0};
    // char web_set_password[100]={0};
    char buf[1000] = {0};
    char buf_t[100];
    printf("INFO_HANDLER:%s,%d in ap_sta_info_handler\n", __FILE__, __LINE__);

    int ret, remaining = req->content_len;
    printf("remaining=%d\n", remaining);
    while (remaining > 0)
    {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                                  MIN(remaining, sizeof(buf)))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        /* Send back the same data */
        // httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;
        printf(TAG, "%.*s\n", ret, buf);
        // for(int i=0;i<ret;i++)
        //{
        //     printf("%d=%2.2x\n",i,buf[i]);
        // }
        /* Log data received */
        // ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        // ESP_LOGI(TAG, "%.*s", ret, buf);
        // ESP_LOGI(TAG, "====================================");
    }
    // sscanf("%s %s",web_set_ssid,web_set_password);
    // printf("web_set_ssid=%s,web_set_password=%s\n",web_set_ssid,web_set_password);
    //  End response
    // httpd_resp_send_chunk(req, NULL, 0);
    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "200 See Other");
    // httpd_resp_set_hdr(req, "Location", "/index");

    // httpd_resp_sendstr(req, "File uploaded successfully");
    //第一步获取AP的信息

    // httpd_resp_sendstr(req, "AP INFO:\nssid:");
    strcpy(buf, "AP INFO\nssid:");
    strcat(buf, EXAMPLE_ESP_WIFIAP_SSID);
    strcat(buf, "\n密码:");
    strcat(buf, EXAMPLE_ESP_WIFIAP_PASS);
    strcat(buf, "\nESP32 AP IP:");

    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip_info;
    while ((netif = esp_netif_next(netif)) != NULL)
    {
        // printf("%s\n",esp_netif_get_desc(netif));

        if (strcmp("ap", esp_netif_get_desc(netif)) == 0)
        {
            esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
            ESP_ERROR_CHECK(ret);
            sprintf(buf_t, "" IPSTR "", IP2STR(&ip_info.ip));
            strcat(buf, buf_t);
        }
    }
    strcat(buf, "\n\nSTA INFO");

    strcat(buf, "\nConnect to AP SSID:");
    wifi_config_t cfg;
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    strcat(buf, (char *)cfg.sta.ssid);
    strcat(buf, "\nConnect to AP PASSWORD:");
    strcat(buf, (char *)cfg.sta.password);
    strcat(buf, "\nESP32 STA IP:");
    while ((netif = esp_netif_next(netif)) != NULL)
    {
        // printf("%s\n",esp_netif_get_desc(netif));

        if (strcmp("sta", esp_netif_get_desc(netif)) == 0)
        {
            esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
            ESP_ERROR_CHECK(ret);
            sprintf(buf_t, "" IPSTR "", IP2STR(&ip_info.ip));
            strcat(buf, buf_t);
        }
    }

    wifi_ap_record_t record;
    esp_wifi_sta_get_ap_info(&record);
    sprintf(buf_t, "\nap rssi:%d\n", record.rssi);
    strcat(buf, buf_t);
    httpd_resp_sendstr(req, (char *)buf);
    // wifi_reconnect(web_set_ssid,web_set_password);
    return ESP_OK;
}

struct async_resp_arg
{
    httpd_handle_t hd;
    int fd;
};

/*
 * async send function, which we put into the httpd work queue
 */
int gcnt = 0;
static void ws_async_send(void *arg)
{
    // static const char * data = "Async data";
    //
    // static int i=0;
    char buf[32] = {0};
    sprintf(buf, "%d %d %d", r, g, b);
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)buf; //(uint8_t*)data;
    ws_pkt.len = strlen(buf);        // strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

static void ws_async_send1(void *arg)
{
    // static const char * data = "Async data";
    //
    // static int i=0;
    char buf[32] = {0};
    sprintf(buf, "%d %d %d", r, g, b);
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)buf; //(uint8_t*)data;
    ws_pkt.len = strlen(buf);        // strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
    printf("In ws_async_send1\n");
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    return httpd_queue_work(handle, ws_async_send, resp_arg);
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
// Get all clients and send async message
static void wss_server_send_messages(httpd_handle_t *server)
{
    bool send_messages = true;

    // Send async message to all connected clients that use websocket protocol every 10 seconds
    // while (send_messages) {
    // vTaskDelay(10000 / portTICK_PERIOD_MS);

    if (!*server)
    { // httpd might not have been created by now
        // continue;
        return;
    }
    size_t clients = MAX_CLIENTS;
    int client_fds[MAX_CLIENTS] = {0};
    if (httpd_get_client_list(*server, &clients, client_fds) == ESP_OK)
    {
        for (size_t i = 0; i < clients; ++i)
        {
            int sock = client_fds[i];
            if (httpd_ws_get_fd_info(*server, sock) == HTTPD_WS_CLIENT_WEBSOCKET)
            {
                ESP_LOGI(TAG, "Active client (fd=%d) -> sending async message", sock);
                struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
                resp_arg->hd = *server;
                resp_arg->fd = sock;
                httpd_queue_work(handle, ws_async_send, resp_arg);
                // ws_async_send(resp_arg);
                // if (httpd_queue_work(resp_arg->hd, send_hello, resp_arg) != ESP_OK) {
                //     ESP_LOGE(TAG, "httpd_queue_work failed!");
                //     send_messages = false;
                //     break;
                // }
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "httpd_get_client_list failed!");
        return;
    }
    //}
}
static esp_err_t echo_handler(httpd_req_t *req)
{
    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@1\n");
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@2\n");
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len)
    {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if (strncmp((char *)ws_pkt.payload, "1234", strlen("1234")) != 0)
    {
        sscanf((char *)ws_pkt.payload, "%d %d %d", &r, &g, &b);
        printf("r=%d,g=%d,b=%d\n", r, g, b);
        rgb_led_set(r, g, b);
        wss_server_send_messages(&handle);
    }
    else
    {
        struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
        resp_arg->hd = handle;
        resp_arg->fd = httpd_req_to_sockfd(req);
        httpd_queue_work(handle, ws_async_send, resp_arg);
    }
    /*
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
        strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
        free(buf);
        return trigger_async_send(req->handle, req);
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    */
    free(buf);
    return ret;
}

static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1)
    {
        ;
    }
}

static void infinite_loop(void)
{
    int i = 0;
    ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
    while (1)
    {
        ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
/*
static esp_err_t ota_handler(httpd_req_t *req)
{
    printf("######################1\n");
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    printf("######################2\n");
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    static char *ota_write_data;
    int data_read=0;
    esp_err_t err;
    esp_err_t ret;
//////////////////////////////////////////////
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

    int binary_file_length = 0;
    int file_total_len=0;

    bool image_header_was_checked = false;
    //首先获取传输文件的大小。
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_BINARY; //

        ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
        if (ws_pkt.len) {
            buf = calloc(1, ws_pkt.len + 1);
            if (buf == NULL) {
                ESP_LOGE(TAG, "Failed to calloc memory for buf");
                return ESP_ERR_NO_MEM;
            }
            else
                printf("con0=%4.4x\n",buf);
            ws_pkt.payload = buf;

            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
                free(buf);
                return ret;
            }
            ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
        }
        ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);

        if(buf!=NULL)
            printf("con1=%2.2x%2.2x%2.2x%2.2x\n",buf[0],buf[1],buf[2],buf[3]);

        //return 0;
        file_total_len=buf[1]+buf[2]*256+buf[3];

        //发送数据
        struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
        resp_arg->hd = handle;
        resp_arg->fd = httpd_req_to_sockfd(req);
        //httpd_queue_work(handle, ws_async_send, resp_arg);
        ws_async_send1(resp_arg);
        //////////

    while (file_total_len>binary_file_length) {
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_BINARY; //

        //vTaskDelay(200);
        ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
        if (ws_pkt.len) {

            buf = calloc(1, ws_pkt.len + 1);
            if (buf == NULL) {
                ESP_LOGE(TAG, "Failed to calloc memory for buf");
                return ESP_ERR_NO_MEM;
            }
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
                free(buf);
                return ret;
            }
            ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
        }
        ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
        for(int i=0;i<ws_pkt.len;i++)
            printf("buf[%d]=%2.2x\n",i,buf[i]);

        data_read=ws_pkt.len;
        ota_write_data=(char *)buf;
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            task_fatal_error();
        } else if (data_read > 0) {
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGW(TAG, "New version is the same as invalid version.");
                            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                            //http_cleanup(client);
                            infinite_loop();
                        }
                    }
#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
                    if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
                        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
                        //http_cleanup(client);
                        infinite_loop();
                    }
#endif

                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        //http_cleanup(client);
                        esp_ota_abort(update_handle);
                        task_fatal_error();
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                } else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    //http_cleanup(client);
                    esp_ota_abort(update_handle);
                    task_fatal_error();
                }
            }
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                //http_cleanup(client);
                esp_ota_abort(update_handle);
                task_fatal_error();
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
            //发送数据
            struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
            resp_arg->hd = handle;
            resp_arg->fd = httpd_req_to_sockfd(req);
            //httpd_queue_work(handle, ws_async_send, resp_arg);
            ws_async_send1(resp_arg);
            //////////
        } else if (data_read == 0) {

            //if (errno == ECONNRESET || errno == ENOTCONN) {
            //    ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
            //    break;
            //}
            //if (esp_http_client_is_complete_data_received(client) == true) {
            //    ESP_LOGI(TAG, "Connection closed");
            //    break;
            //}
            printf("NOT EXCEPT-1!!!!!!\n");
        }
    }
    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);
    //if (esp_http_client_is_complete_data_received(client) != true) {
    //    ESP_LOGE(TAG, "Error in receiving complete file");
    //    //http_cleanup(client);
    //    esp_ota_abort(update_handle);
    //    task_fatal_error();
    //    printf("NOT EXCEPT-2!!!!!!\n");
    //}

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        //http_cleanup(client);
        task_fatal_error();
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        //http_cleanup(client);
        task_fatal_error();
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    return ESP_OK;
    free(buf);
    return ret;
}
*/
esp_ota_handle_t update_handle = 0;
const esp_partition_t *update_partition = NULL;
const esp_partition_t *configured;
const esp_partition_t *running;

int binary_file_length = 0;
int file_total_len = 0;
bool image_header_was_checked;
static esp_err_t ota_handler(httpd_req_t *req)
{
    printf("######################1\n");
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    printf("######################2\n");
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    static char *ota_write_data;
    int data_read = 0;
    esp_err_t err;
    esp_err_t ret;
    //////////////////////////////////////////////
    //
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY; //

    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        else
            printf("con0=%4.4x\n", buf);
        ws_pkt.payload = buf;

        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if (buf != NULL)
        printf("con1=%2.2x%2.2x%2.2x%2.2x\n", buf[0], buf[1], buf[2], buf[3]);
    // buf[0],buf[1] == 0x00,0x00;   首包数据
    if (buf[0] == 0 && buf[1] == 0)
    {
        configured = esp_ota_get_boot_partition();
        running = esp_ota_get_running_partition();
        if (configured != running)
        {
            ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                     configured->address, running->address);
            ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
        }
        ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
                 running->type, running->subtype, running->address);

        update_partition = esp_ota_get_next_update_partition(NULL);
        assert(update_partition != NULL);
        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
                 update_partition->subtype, update_partition->address);

        image_header_was_checked = false;
    }
    data_read = ws_pkt.len - 2;
    ota_write_data = (char *)(buf + 2);

    if (data_read < 0)
    {
        ESP_LOGE(TAG, "Error: SSL data read error");
        task_fatal_error();
    }
    else if (data_read > 0)
    {
        if (image_header_was_checked == false)
        {
            esp_app_desc_t new_app_info;
            if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
            {
                // check current version with downloading
                memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                esp_app_desc_t running_app_info;
                if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
                {
                    ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                }

                const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
                esp_app_desc_t invalid_app_info;
                if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK)
                {
                    ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                }

                // check current version with last invalid partition
                if (last_invalid_app != NULL)
                {
                    if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0)
                    {
                        ESP_LOGW(TAG, "New version is the same as invalid version.");
                        ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                        ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                        // http_cleanup(client);
                        infinite_loop();
                    }
                }
#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
                if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0)
                {
                    ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
                    // http_cleanup(client);
                    infinite_loop();
                }
#endif

                image_header_was_checked = true;

                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                    // http_cleanup(client);
                    esp_ota_abort(update_handle);
                    task_fatal_error();
                }
                ESP_LOGI(TAG, "esp_ota_begin succeeded");
            }
            else
            {
                ESP_LOGE(TAG, "received package is not fit len");
                // http_cleanup(client);
                esp_ota_abort(update_handle);
                task_fatal_error();
            }
        }
        err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
        if (err != ESP_OK)
        {
            // http_cleanup(client);
            esp_ota_abort(update_handle);
            task_fatal_error();
        }
        binary_file_length += data_read;
        ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        //发送数据
        struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
        resp_arg->hd = handle;
        resp_arg->fd = httpd_req_to_sockfd(req);
        // httpd_queue_work(handle, ws_async_send, resp_arg);
        ws_async_send1(resp_arg);
        //////////
    }
    else if (data_read == 0)
    {

        // if (errno == ECONNRESET || errno == ENOTCONN) {
        //     ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
        //     break;
        // }
        // if (esp_http_client_is_complete_data_received(client) == true) {
        //     ESP_LOGI(TAG, "Connection closed");
        //     break;
        // }
        printf("NOT EXCEPT-1!!!!!!\n");
    }
    if(buf[0]==0&&buf[1]==0xff)
    {
        err = esp_ota_end(update_handle);
        if (err != ESP_OK)
        {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED)
            {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            else
            {
                ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
            }
            // http_cleanup(client);
            task_fatal_error();
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
            // http_cleanup(client);
            task_fatal_error();
        }
        ESP_LOGI(TAG, "Prepare to restart system!");
        esp_restart();
    }
    else
    {
        free(buf);
    }
    return ESP_OK;
}


esp_err_t start_server()
{

    httpd_config_t httpd_config_var = HTTPD_DEFAULT_CONFIG();
    httpd_config_var.max_open_sockets = MAX_CLIENTS;
    if (httpd_start(&handle, &httpd_config_var) != ESP_OK)
    {
        printf("Server Start Failed\n");
        return ESP_FAIL;
    }
    else
    {
        printf("Server Start Successed\n");
    }

    httpd_uri_t index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(handle, &index);
    httpd_uri_t index_ico = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = ico_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(handle, &index_ico);
    httpd_uri_t username = {
        .uri = "/username",
        .method = HTTP_POST,
        .handler = username_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(handle, &username);
    httpd_uri_t ap_sta_info = {
        .uri = "/ap_sta_info",
        .method = HTTP_POST,
        .handler = ap_sta_info_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(handle, &ap_sta_info);

    static const httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = echo_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true};
    httpd_register_uri_handler(handle, &ws);

    static const httpd_uri_t ws1 = {
        .uri = "/ws1",
        .method = HTTP_GET,
        .handler = ota_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true};
    httpd_register_uri_handler(handle, &ws1);

    return ESP_OK;
}

void send_to_all(void *p)
{
    while (1)
    {
        // wss_server_send_messages(&handle);
        sys_delay_ms(500);
        gcnt++;
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(init_spiffs());

    const esp_partition_t *running = esp_ota_get_running_partition();
    printf("running address=0x%8.8x\n", running->address);
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    rgb_led_init();
    rgb_led_set(r, g, b);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 70000, (void *)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 40960, (void *)AF_INET6, 5, NULL);
#endif
    start_server();
    sys_delay_ms(10000);
    esp_netif_t *netif = NULL;

    while ((netif = esp_netif_next(netif)) != NULL)
    {
        printf("%s\n", esp_netif_get_desc(netif));
        esp_netif_ip_info_t ip_info;
        esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
        // printf("  A   : " IPSTR "\n", IP2STR(&(a->addr.u_addr.ip4)));
        if (ret == ESP_OK)
            printf("%s:" IPSTR "\n", esp_netif_get_desc(netif), IP2STR(&ip_info.ip));
        else
            printf("ERROR,ret=%d,%s\n", ret, esp_netif_get_desc(netif));
    }

    xTaskCreate(send_to_all, "send_to_all", 4096, NULL, 5, NULL);
}
