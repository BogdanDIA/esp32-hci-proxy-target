/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_err.h"

#include "esp_bt.h"
#include "soc/uhci_periph.h"
#include "esp_private/periph_ctrl.h" // for enabling UHCI module, remove it after UHCI driver is released

//#define HCI_PROTO_DEBUG 1
#define HCI_PROTO_TEST 1

extern esp_err_t do_console_provision(bool, bool);

static const int RX_BUF_SIZE = 1024;
static const char *TAG = "HCI-IP";
static const char *tag = "CONTROLLER_HCI-IP";

static volatile int c_sock;
static volatile struct sockaddr_storage c_source_addr; // Large enough for both IPv4 or IPv6

/*
 * @brief: Show reset reason 
 */

void show_reset_reason()
{
    esp_reset_reason_t rst_r = esp_reset_reason();
    switch (rst_r)
    {
      case ESP_RST_UNKNOWN:
        ESP_LOGE(tag, "ESP_RST_UNKNOWN");
        break;
      case ESP_RST_POWERON:
        ESP_LOGE(tag, "ESP_RST_POWERON");
        break;
      case ESP_RST_EXT:
        ESP_LOGE(tag, "ESP_RST_EXT");
        break;
      case ESP_RST_SW:
        ESP_LOGE(tag, "ESP_RST_SW");
        break;
      case ESP_RST_PANIC:
        ESP_LOGE(tag, "ESP_RST_PANIC");
        break;
      case ESP_RST_INT_WDT:
        ESP_LOGE(tag, "ESP_RST_INT_WDT");
        break;
      case ESP_RST_TASK_WDT:
        ESP_LOGE(tag, "ESP_RST_TASK_WDT");
        break;
      case ESP_RST_DEEPSLEEP:
        ESP_LOGE(tag, "ESP_RST_DEEPSLEEP");
        break;
      case ESP_RST_BROWNOUT:
        ESP_LOGE(tag, "ESP_RST_BROWNOUT");
        break;
      case ESP_RST_SDIO:
        ESP_LOGE(tag, "ESP_RST_SDIO");
        break;
      case ESP_RST_JTAG:
        ESP_LOGE(tag, "ESP_RST_JTAG");
        break;
      case ESP_RST_PWR_GLITCH:
        ESP_LOGE(tag, "ESP_RST_PWR_GLITCH");
        break;
      case ESP_RST_CPU_LOCKUP:
        ESP_LOGE(tag, "ESP_RST_CPU_LOCKUP");
        break;
      default:
        ESP_LOGE(tag, "Unknown source");
    }
}

/*
 * @brief: BT controller callback function, used to notify the upper layer that
 *         controller is ready to receive command
 */
static void controller_rcv_pkt_ready(void)
{
    //printf("controller rcv pkt ready\n");
}

/*
 * @brief: BT controller callback function, to transfer data packet to upper
 *         controller is ready to receive command
 */
/*
*/

static const char *RX_HCI_CB = "RX_HCI_CB";

static int host_rcv_pkt(uint8_t *data, uint16_t len)
{
    if (len > 0 && len < RX_BUF_SIZE)
    {
      int txBytes = 0;
      do
      {
        txBytes = sendto(c_sock, &data[txBytes], len, 0, (struct sockaddr *)&c_source_addr, sizeof(c_source_addr));
        if (txBytes < 0) {
          ESP_LOGE(RX_HCI_CB, "Error occurred during sendto: errno %d", errno);
          return -1;
        }
#ifdef HCI_PROTO_DEBUG
        else if (txBytes < len)
          ESP_LOGI(RX_HCI_CB, "More data to send upstream UDP: %i, %i", txBytes, len);
        else
          ESP_LOGI(RX_HCI_CB, "Data sent finished UDP: %i", len);

        ESP_LOGI(RX_HCI_CB, "Last data: len: %i, txBytes: %i", len, txBytes);
        ESP_LOG_BUFFER_HEXDUMP(RX_HCI_CB, data, len, ESP_LOG_INFO);
#endif
        len -= txBytes;
      } while (len > 0);
    }
 
    return 0;
}

static esp_vhci_host_callback_t vhci_host_cb = {
    controller_rcv_pkt_ready,
    host_rcv_pkt
};

#define PORT                        CONFIG_HCI_IP_PORT

static void udp_server_task(void *pvParameters)
{
    static const char *RX_TASK_TAG = "UDP_RX_TASK";
    uint8_t* rx_buffer = (uint8_t*) malloc(RX_BUF_SIZE + 1);
    int addr_family = AF_INET;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    /* Register the callbacks used by controller */
    esp_vhci_host_register_callback(&vhci_host_cb);

    while (1) {

        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        c_sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (c_sock < 0) {
            ESP_LOGE(RX_TASK_TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(RX_TASK_TAG, "Socket created");

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        int enable = 1;
        lwip_setsockopt(c_sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

#if defined(CONFIG_HCI_IP_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(c_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(c_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif

        int err = bind(c_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(RX_TASK_TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(RX_TASK_TAG, "Socket bound, port %d", PORT);

        socklen_t c_socklen = sizeof(c_source_addr);

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        struct iovec iov;
        struct msghdr msg;
        struct cmsghdr *cmsgtmp;
        u8_t cmsg_buf[CMSG_SPACE(sizeof(struct in_pktinfo))];

        iov.iov_base = rx_buffer;
        iov.iov_len = sizeof(rx_buffer);
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        msg.msg_flags = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = (struct sockaddr *)&c_source_addr;
        msg.msg_namelen = c_socklen;

        ESP_LOGI(RX_TASK_TAG, "CONFIG_LWIP_NETBUF_RECVINFO defined");
#endif
 
        ESP_LOGI(RX_TASK_TAG, "Waiting for UDP data");

        while (1) {
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
            int len = recvmsg(c_sock, &msg, 0);
#else
            int len = recvfrom(c_sock, rx_buffer, RX_BUF_SIZE - 1, 0, (struct sockaddr *)&c_source_addr, &c_socklen);
#endif
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(RX_TASK_TAG, "Error occured during recvfrom: errno %d", errno);
                break;
            }
            else {
              // Data received
#ifdef HCI_PROTO_DEBUG
              ESP_LOGI(RX_TASK_TAG, "Received from socket %d bytes", len);
              ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, rx_buffer, len, ESP_LOG_INFO);
#endif

#ifdef HCI_PROTO_TEST
              // if we received TEST packet, reply back on the socket
              if (rx_buffer[0] == 0x0a)
              {
                // TODO - handle partial write
                sendto(c_sock, &rx_buffer[0], len, 0, (struct sockaddr *)&c_source_addr, sizeof(c_source_addr));
                continue;
              }
#endif
 
              bool send_available= esp_vhci_host_check_send_available();
              if (send_available)
                esp_vhci_host_send_packet(rx_buffer, len);
              else
                ESP_LOGE(RX_TASK_TAG, "esp_vhci not available for sending\n");
            }
        }

        if (c_sock != -1) {
            ESP_LOGE(RX_TASK_TAG, "Shutting down socket and restarting...");
            shutdown(c_sock, 0);
            close(c_sock);
        }
    }
    free(rx_buffer);
    vTaskDelete(NULL);
}

#define CONFIG_PROVISIONING_SIZE 10

static void serial_prov_task(void *pvParameters)
{
  while(1)
  {
    int count_n = 0;
    int count_f = 0;
    char c;

    while ((c = getc(stdin)) != '\n')
    {
      {
        // n...n
        if (c == 0x6E)
          count_n++;
        // f...f
        if (c == 0x66)
          count_f++;
      }
    }

    if (count_n >= CONFIG_PROVISIONING_SIZE)
    {
      ESP_LOGI(TAG, "Going to provision WiFi now");
      if (ESP_OK != do_console_provision(true, true))
        ESP_LOGE(TAG, "Provision WiFi now failed");
    } 
    else if (count_f >= CONFIG_PROVISIONING_SIZE)
    {
      ESP_LOGI(TAG, "Going to provision WiFi after connection retries end");
      if (ESP_OK != do_console_provision(false, true))
        ESP_LOGE(TAG, "Provision WiFi late failed");
    }
  }
}

void app_main(void)
{
    show_reset_reason();

    esp_err_t ret;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    xTaskCreatePinnedToCore(&serial_prov_task, "serial_prov_task", 4096, NULL, 0, NULL, 0);

    ESP_ERROR_CHECK(example_connect());

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGI(tag, "Bluetooth controller release classic bt memory failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Bluetooth Controller initialize failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Bluetooth Controller initialize failed: %s", esp_err_to_name(ret));
        return;
    }
    
#ifdef CONFIG_HCI_IP_IPV4
    xTaskCreatePinnedToCore(&udp_server_task, "udp_server_task", 4096, NULL, 5, NULL, 0);
#endif
}
