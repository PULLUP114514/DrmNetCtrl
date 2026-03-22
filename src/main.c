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
    CONTOL_BRIGHTNESS = 1, // Operation Data 为一个int 代表目标屏幕亮度
    CONTOL_EXIT,           // 退出Drm_App

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
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>

#define PATH_MAX 4096
#define BUF_SIZE 1024
#define BACKLOG 5
#define PORT 1572

void notice_with_warning(ipc_client_t *client, const char *title, const char *desc)
{
    if (ipc_client_ui_warning(client, title, desc, UI_ICON_CAT, 0xFF004400) < 0)
    {
        log_error("ipc_client_ui_warning failed");
    }
}

int InitTcpServer(int port);
int MessageProcesser(char *message, ipc_client_t *ipcClient);
int SetBrightness(int brightness, ipc_client_t *ipcClient);
int GetInt(char *data, int offset, uint32_t size);
int InitIPC(ipc_client_t *ipcClient);
int CheckMutex();

int main(int argc, char *argv[])
{

    // TODO：Progress Mutex
    CheckMutex();
    // init TCP Server
    int sockFD = InitTcpServer(PORT);
    if (sockFD == -1)
    {
        log_error("Start TCP Server Failed.");
        return 1;
    }
    log_info("Server listening on %d...\n", PORT);

    struct sockaddr_in clientAddress;
    socklen_t clientLen = sizeof(clientAddress);
    char buffer[BUF_SIZE];
    bool reinitIPC = true;
    ipc_client_t ipcClient = {.fd = -1};
    // 主TCP循环
    while (1)
    {
        // init IPC
        int client_fd = accept(sockFD,
                               (struct sockaddr *)&clientAddress,
                               &clientLen);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        log_info("Client: %s:%d\n",
                 inet_ntoa(clientAddress.sin_addr),
                 ntohs(clientAddress.sin_port));

        // notice_with_warning(&ipcClient,
        //                     "受到连接",
        //                     inet_ntoa(clientAddress.sin_addr));
        while (1)
        {
            int n = 0;
            if (reinitIPC)
            {
                ipcClient.fd = -1;
                if (InitIPC(&ipcClient) != 0)
                {
                    log_error("Init IPC Failed");
                    usleep(3 * 1000 * 1000);
                    continue;
                }
                reinitIPC = false;
                notice_with_warning(&ipcClient, "已启动IPC", "不建议在IPC正常工作时再次启动\n仅建议在无应答时再次运行以重启");
            }
            n = read(client_fd, buffer, BUF_SIZE - 1);
            if (n <= 0)
                break;

            buffer[n] = '\0';
            int success = MessageProcesser(buffer, &ipcClient);
            if (success != 0)
            {
                reinitIPC = true;
                continue;
            }
        }

        close(client_fd);
        log_info("Client disconnected\n");
    }
    return 0;
}

int InitIPC(ipc_client_t *ipcClient)
{
    // init IPC
    if (ipc_client_init(ipcClient) < 0)
    {
        log_error("ipc_client_init failed");
        return 1;
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

int MessageProcesser(char *message, ipc_client_t *ipcClient)
{
    int tempInt = 0;
    uint8_t packageControlID = 0;
    memcpy(&packageControlID, message, sizeof(uint8_t));
    log_info("get Opreation Code %d", packageControlID);
    switch (packageControlID)
    {
    case CONTOL_BRIGHTNESS:
        memcpy(&tempInt, message + sizeof(uint8_t) * 3, sizeof(int));
        return SetBrightness(tempInt, ipcClient);
    case CONTOL_EXIT:
        if (ipc_client_app_exit(ipcClient, EXITCODE_SRGN_CONFIG) < 0)
        {
            log_error("ipc_client_app_exit failed");
            return 1;
        }
        return 0;
    default:
        return -1;
    }
    return 0;
}

/// @brief 转换为int 失败返回 -114514
/// @param data
/// @param offset
/// @param size
/// @return
int GetInt(char *data, int offset, uint32_t size)
{
    char *endptr;
    unsigned char temp[size + 1];
    memcpy(temp, data + offset, size);
    int intData = strtol(temp, &endptr, 10);
    if (errno == ERANGE || endptr == temp || *endptr != '\0')
    {
        log_error("NOT A NUMBER! What the hell are you doing?\n");
        return -114514;
    }
    return intData;
}

int SetBrightness(int brightness, ipc_client_t *ipcClient)
{
    ipc_settings_data_t settings = {0};
    if (ipc_client_settings_get(ipcClient, &settings) == 0)
    {
        log_info("brightness set to : %d", brightness);
        settings.brightness = brightness;
        if (ipc_client_settings_set(ipcClient, &settings) < 0)
        {
            log_error("ipc_client_settings_set failed");
            return 1;
        }
        usleep(1 * 1000 * 1000);
    }
    else
    {
        log_error("ipc_client_settings_get failed");
        return 1;
    }
    return 0;
}

/// @brief 返回-1
/// @return
int CheckMutex()
{

    DIR *dir = opendir("/proc");
    struct dirent *entry;
    int pidList[128] = {0};
    int count = 0;
    int temp = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        // 判断是否是数字（PID）
        if (isdigit(entry->d_name[0]))
        {
            char path[256];
            snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);

            FILE *fp = fopen(path, "r");
            if (fp)
            {
                char name[256];
                fgets(name, sizeof(name), fp);

                // 去掉换行
                name[strcspn(name, "\n")] = 0;

                if (strcmp(name, "IpcController") == 0)
                {
                    char *endptr;
                    long pid = strtol(entry->d_name, &endptr, 10);
                    if (*endptr == '\0')
                    {
                        log_info("Found PID: %ld\n", pid);
                        pidList[count] = pid;
                        count++;
                    }
                }
                fclose(fp);
            }
        }
    }

    closedir(dir);
    return -1;
}
