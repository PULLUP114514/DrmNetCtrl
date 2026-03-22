/*
 *  此处用于连接Drm_neo_app 与 上位机
 *
 *
 * 包定义 (不超过BUF_SIZE) ：
 *   +------------------+-------------------+--------------------+
 *   |  Operation Code  |  Operation size   |   Operation Data   |
 *   |      uint8       |      uint 16      |    unsigned char   |
 *   +------------------+-------------------+--------------------+
 *  Operation Code 见枚举
 */

// 定义一个枚举类型
typedef enum
{
    CONTOL_BRIGHTNESS = 1,
    STATUS_RUNNING,
    STATUS_ERROR,
    STATUS_DONE
} OPERATION_CODE_ENUM;

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ipc_client.h"
#include "ipc_common.h"
#include "log.h"
#include "uuid.h"
#include "icons.h"
#include <arpa/inet.h>
#include <errno.h>

#define PATH_MAX 4096
#define BUF_SIZE 1024
#define BACKLOG 5
#define PORT 1572

static int build_asset_path(char *out, size_t out_len, const char *filename)
{
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        log_error("getcwd failed");
        return -1;
    }
    int written = snprintf(out, out_len, "%s/assets/%s", cwd, filename);
    if (written < 0 || (size_t)written >= out_len)
    {
        log_error("asset path too long: %s", filename);
        return -1;
    }
    return 0;
}

static void print_settings(const ipc_settings_data_t *settings)
{
    log_info(
        "settings: brightness=%d interval=%d mode=%d usb=%d ctrl_lowbat=%u ctrl_no_intro=%u ctrl_no_overlay=%u",
        settings->brightness,
        settings->switch_interval,
        settings->switch_mode,
        settings->usb_mode,
        settings->ctrl_word.lowbat_trip,
        settings->ctrl_word.no_intro_block,
        settings->ctrl_word.no_overlay_block);
}

void notice_with_warning(ipc_client_t *client, const char *title, const char *desc)
{
    if (ipc_client_ui_warning(client, title, desc, UI_ICON_CAT, 0xFF004400) < 0)
    {
        log_error("ipc_client_ui_warning failed");
    }
}

int InitTcpServer(int port);

void MessageProcesser(char *message, ipc_client_t *ipcClient);

void SetBrightness(int brightness, ipc_client_t *ipcClient);

int GetInt(char *data, int offset, uint32_t size);

int main(int argc, char *argv[])
{

    // TODO：Progress Mutex

    // init IPC
    ipc_client_t ipcClient = {.fd = -1};
    if (ipc_client_init(&ipcClient) < 0)
    {
        log_error("ipc_client_init failed");
        return 1;
    }
    int rc = 0;
    notice_with_warning(&ipcClient, "已启动IPC", "不建议在IPC正常工作时再次启动\n仅建议在无应答时再次运行以重启");

    // init TCP Server
    int sockFD = InitTcpServer(PORT);
    if (sockFD == -1)
    {
        log_error("Start TCP Server Failed.");
        return 1;
    }
    log_info("Server listening on %d...\n", PORT);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUF_SIZE];

    // 主TCP循环
    while (1)
    {
        int client_fd = accept(sockFD,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        log_info("Client: %s:%d\n",
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port));

        notice_with_warning(&ipcClient,
                            "受到连接",
                            inet_ntoa(client_addr.sin_addr));
        while (1)
        {
            int n = read(client_fd, buffer, BUF_SIZE - 1);
            if (n <= 0)
                break;

            buffer[n] = '\0';
            MessageProcesser(buffer, &ipcClient);
            write(client_fd, buffer, n); // echo
        }

        close(client_fd);
        log_info("Client disconnected\n");
    }
    return 0;
}

int InitTcpServer(int port)
{
    // 错误缓冲区
    char errbuf[128];

    int server_fd;
    struct sockaddr_in addr;

    // 1. 创建 socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        snprintf(errbuf, sizeof(errbuf), "socket error: %s", strerror(errno));
        log_error(errbuf);
        return -1;
    }

    // 可选：端口复用（避免重启时报 Address already in use）
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. 配置地址
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // 3. 绑定
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return -1;
    }

    // 4. 监听
    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

void MessageProcesser(char *message, ipc_client_t *ipcClient)
{
    int size = 5;

    printf("Hex: ");
    for (int i = 0; i < size; i++)
    {
        printf("%02X ", (unsigned char)message[i]);
    }
    printf("\n");

    int tempInt = 0;
    uint8_t packageControlID = 0;
    memcpy(&packageControlID, message, sizeof(uint8_t));
    log_info("get Opreation Code %d", packageControlID);
    switch (packageControlID)
    {
    case CONTOL_BRIGHTNESS:
        memcpy(&tempInt, message + sizeof(uint8_t) * 3, sizeof(int));
        SetBrightness(tempInt, ipcClient);
        break;

    default:
        return;
    }
    return;
}

int GetInt(char *data, int offset, uint32_t size)
{
    char *endptr;
    unsigned char temp[size + 1];
    memcpy(temp, data + offset, size);
    int intData = strtol(temp, &endptr, 10);
    if (errno == ERANGE || endptr == temp || *endptr != '\0')
    {
        log_error("NOT A NUMBER! What the hell are you doing?\n");
        return -1;
    }
    return intData;
}

void SetBrightness(int brightness, ipc_client_t *ipcClient)
{
    ipc_settings_data_t settings = {0};
    if (ipc_client_settings_get(ipcClient, &settings) == 0)
    {
        print_settings(&settings);
        log_info("brightness set to : %d", brightness);
        settings.brightness = brightness;
        if (ipc_client_settings_set(ipcClient, &settings) < 0)
        {
            log_error("ipc_client_settings_set failed");
        }
        usleep(1 * 1000 * 1000);
    }
    else
    {
        log_error("ipc_client_settings_get failed");
    }
    return;
}
