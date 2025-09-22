/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Common functions for protocol examples, to establish Wi-Fi or Ethernet connection.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
 */

#include <string.h>
#include "protocol_examples_common.h"
#include "example_common_private.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "rom/uart.h"

#if CONFIG_EXAMPLE_CONNECT_WIFI

static const char *TAG = "HCI-IP_connect";
static esp_netif_t *s_example_sta_netif = NULL;
static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;
#if CONFIG_EXAMPLE_CONNECT_IPV6
static SemaphoreHandle_t s_semph_get_ip6_addrs = NULL;
#endif

static int s_retry_num = 0;
static bool stop_wifi_retry = false;

/*
 * @brief: Get WiFi SSID and password input from serial console and store in NVS
 * params: provisin_now
 * params: provision reset
 */
esp_err_t do_console_provision(bool provision_now, bool provision_reset)
{
  static bool _provision_done = true;

  if (provision_reset)
    _provision_done = false;

  if (provision_now)
  {
    stop_wifi_retry = true;
    ESP_LOGD(TAG, "Stop WiFi connect retry");
  }
  else
    return ESP_OK;

  if (provision_now && _provision_done)
    return ESP_OK; 

  // read wifi config from NVS
  wifi_config_t wifi_config;
  esp_err_t stat = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
  if (stat != ESP_OK)
  {
    ESP_LOGE(TAG, "Cannot read wifi_config from NVS");
    return stat;
  }

  char buf[sizeof(wifi_config.sta.ssid)+sizeof(wifi_config.sta.password)+2] = {0};

  // test console not to lose initial chars
  // TODO fix bug with ESP_LOG preventing first input char to be received
  char * test_str="abcdefg";

  for (uint8_t i = 0; i < 5; i++)
  {
    memset(buf, 0, sizeof(buf));
    ESP_LOGI(TAG, "Test console[%i]: type %s in terminal", i, test_str);
    fgets(buf, sizeof(buf), stdin);
    uint16_t len = strlen(buf);
    buf[len-1] = '\0'; /* removes '\n' */
    if (strncmp(buf, test_str, strlen(test_str)))
      printf("Read from console[%i]:%s not OK, will retry\n", i, buf); 
    else
    {
      printf("Read from console[%i]:%s OK, will continue\n", i, buf); 
      break;
    }
  }

  ESP_LOGI(TAG, "Please input ssid password:");
  memset(buf, 0, sizeof(buf));
  fgets(buf, sizeof(buf), stdin);
  int len = strlen(buf);
  buf[len-1] = '\0'; /* removes '\n' */
  memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));

  char *rest = NULL;
  char *temp = strtok_r(buf, " ", &rest);
  if (temp == NULL)
  {
    ESP_LOGI(TAG, "Invalid input");
    return ESP_FAIL;
  }
  strncpy((char*)wifi_config.sta.ssid, temp, sizeof(wifi_config.sta.ssid));
  memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
  temp = strtok_r(NULL, " ", &rest);
  if (temp) {
      strncpy((char*)wifi_config.sta.password, temp, sizeof(wifi_config.sta.password));
      ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
      esp_err_t stat = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
      if (stat != ESP_OK)
      {
        ESP_LOGE(TAG, "Cannot write credentials in NVS");
        return stat;
      } 

      stat = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
      if (stat != ESP_OK)
      {
        ESP_LOGE(TAG, "Cannot read credentials from NVS");
        return stat;
      }

      ESP_LOGI(TAG, "WIFI SSID: %s", (char *) wifi_config.sta.ssid);
      ESP_LOGI(TAG, "WIFI Password: %s", (char *) wifi_config.sta.password);
  } else {
      wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  _provision_done = true;
  stop_wifi_retry = false;

  // restart ESP
  esp_restart();

  return ESP_OK;
}

static void example_handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    s_retry_num++;
    if (0 /*s_retry_num > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY*/) {
        ESP_LOGI(TAG, "WiFi Connect failed %d times, stop reconnect.", s_retry_num);
        /* let example_wifi_sta_do_connect() return */
        if (s_semph_get_ip_addrs) {
            xSemaphoreGive(s_semph_get_ip_addrs);
        }
#if CONFIG_EXAMPLE_CONNECT_IPV6
        if (s_semph_get_ip6_addrs) {
            xSemaphoreGive(s_semph_get_ip6_addrs);
        }
#endif
        return;
    }

    if (!stop_wifi_retry)
    {
        ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
        esp_err_t err = esp_wifi_connect();
        if (err == ESP_ERR_WIFI_NOT_STARTED) {
            return;
        }
        ESP_ERROR_CHECK(err);
    }
}

static void example_handler_on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi connected");

#if CONFIG_EXAMPLE_CONNECT_IPV6
    esp_netif_create_ip6_linklocal(esp_netif);
#endif // CONFIG_EXAMPLE_CONNECT_IPV6
}

static void example_handler_on_sta_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!example_is_our_netif(EXAMPLE_NETIF_DESC_STA, event->esp_netif)) {
        return;
    }
    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    if (s_semph_get_ip_addrs) {
        xSemaphoreGive(s_semph_get_ip_addrs);
    } else {
        ESP_LOGI(TAG, "- IPv4 address: " IPSTR ",", IP2STR(&event->ip_info.ip));
    }
}

#if CONFIG_EXAMPLE_CONNECT_IPV6
static void example_handler_on_sta_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    if (!example_is_our_netif(EXAMPLE_NETIF_DESC_STA, event->esp_netif)) {
        return;
    }
    esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
    ESP_LOGI(TAG, "Got IPv6 event: Interface \"%s\" address: " IPV6STR ", type: %s", esp_netif_get_desc(event->esp_netif),
             IPV62STR(event->ip6_info.ip), example_ipv6_addr_types_to_str[ipv6_type]);

    if (ipv6_type == EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE) {
        if (s_semph_get_ip6_addrs) {
            xSemaphoreGive(s_semph_get_ip6_addrs);
        } else {
            ESP_LOGI(TAG, "- IPv6 address: " IPV6STR ", type: %s", IPV62STR(event->ip6_info.ip), example_ipv6_addr_types_to_str[ipv6_type]);
        }
    }
}
#endif // CONFIG_EXAMPLE_CONNECT_IPV6


void example_wifi_start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    esp_netif_config.if_desc = EXAMPLE_NETIF_DESC_STA;
    esp_netif_config.route_prio = 128;
    s_example_sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}


void example_wifi_stop(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_example_sta_netif));
    esp_netif_destroy(s_example_sta_netif);
    s_example_sta_netif = NULL;
}


esp_err_t example_wifi_sta_do_connect(wifi_config_t wifi_config, bool wait)
{
    if (wait) {
        s_semph_get_ip_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip_addrs == NULL) {
            return ESP_ERR_NO_MEM;
        }
#if CONFIG_EXAMPLE_CONNECT_IPV6
        s_semph_get_ip6_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip6_addrs == NULL) {
            vSemaphoreDelete(s_semph_get_ip_addrs);
            return ESP_ERR_NO_MEM;
        }
#endif
    }
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &example_handler_on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &example_handler_on_sta_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &example_handler_on_wifi_connect, s_example_sta_netif));
#if CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &example_handler_on_sta_got_ipv6, NULL));
#endif

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed! ret:%x", ret);
        return ret;
    }
    if (wait) {
        ESP_LOGI(TAG, "Waiting for IP(s)");
#if CONFIG_EXAMPLE_CONNECT_IPV4
        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
#endif
#if CONFIG_EXAMPLE_CONNECT_IPV6
        xSemaphoreTake(s_semph_get_ip6_addrs, portMAX_DELAY);
#endif
        if (s_retry_num > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY) {
          // provision_now=true, provision_reset=false 
          esp_err_t status = do_console_provision(true, false);
          if (status != ESP_OK)
            return status;
        }
    }
    return ESP_OK;
}

esp_err_t example_wifi_sta_do_disconnect(void)
{
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &example_handler_on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &example_handler_on_sta_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &example_handler_on_wifi_connect));
#if CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &example_handler_on_sta_got_ipv6));
#endif
    if (s_semph_get_ip_addrs) {
        vSemaphoreDelete(s_semph_get_ip_addrs);
    }
#if CONFIG_EXAMPLE_CONNECT_IPV6
    if (s_semph_get_ip6_addrs) {
        vSemaphoreDelete(s_semph_get_ip6_addrs);
    }
#endif
    return esp_wifi_disconnect();
}

void example_wifi_shutdown(void)
{
    example_wifi_sta_do_disconnect();
    example_wifi_stop();
}

esp_err_t example_wifi_connect(void)
{
    ESP_LOGI(TAG, "Start example_connect.");
    example_wifi_start();
    wifi_config_t wifi_config = {
        .sta = {
#if !CONFIG_EXAMPLE_WIFI_SSID_PWD_FROM_STDIN
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
#endif
            .scan_method = EXAMPLE_WIFI_SCAN_METHOD,
            .sort_method = EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD,
            .threshold.rssi = CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
#if CONFIG_EXAMPLE_WIFI_SSID_PWD_FROM_STDIN
    example_configure_stdin_stdout();

    wifi_config_t wifi_c;
    esp_err_t stat = ESP_OK;

    // read credentials from NVS
    stat = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_c);
    if (stat != ESP_OK)
    {
      ESP_LOGE(TAG, "Cannot read credentials from NVS");
      return stat;
    }

    // Check if NVS contain SSID and password
    if ((unsigned char)wifi_c.sta.ssid[0] == 0 || (unsigned char)wifi_c.sta.password[0] == 0)
    {
      ESP_LOGE(TAG, "WiFi credentials in NVS do not exist");
      // get credentials from user and store in NVS
      // provision_now=true, provison_reset=true
      stat = do_console_provision(true, true);
      if (stat != ESP_OK)
        return stat;
    }

    // read credentials from NVS
    stat = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_c);
    if (stat != ESP_OK)
    {
      ESP_LOGE(TAG, "Cannot read credentials from NVS");
      return stat;
    }

    /*
    ESP_LOGI(TAG, "WIFI SSID: %s, (char *) wifi_c.sta.ssid);
    ESP_LOGI(TAG, "WIFI Password: %s", (char *) wifi_c.sta.password);
    */

    memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.ssid, (const char *)wifi_c.sta.ssid, sizeof(wifi_config.sta.ssid));

    memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    strncpy((char*)wifi_config.sta.password, (const char *)wifi_c.sta.password, sizeof(wifi_config.sta.password));
#endif
    return example_wifi_sta_do_connect(wifi_config, true);
}


#endif /* CONFIG_EXAMPLE_CONNECT_WIFI */
