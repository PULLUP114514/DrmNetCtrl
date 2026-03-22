/*
 *  此处用于连接Drm_neo_app 与 上位机
 *
 *
 * 包定义 (不超过BUF_SIZE) ：
 *  +------------------+-------------------+------------------------+-------------------------+------------------------+-------------------------+
 *  |  Operation Code  |  Operation Count  |  Operation 1 $Length   |      Operation 1        |  Operation n $Length   |      Operation n        |
 *  |      uInt8       |      uint 8       |       uint 16          |  unsigned Char $Length  |       uint 16          |  unsigned Char $Length  |
 *  +------------------+-------------------+------------------------+-------------------------+------------------------+-------------------------+
 *
 */

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

void MessageProcesser(char *message);

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
            log_info("Recv: %s\n", buffer);

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

void MessageProcesser(char *message)
{

    return;
}
