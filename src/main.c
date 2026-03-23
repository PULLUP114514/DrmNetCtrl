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

#include <signal.h>
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
#include <dirent.h>
#include <ctype.h>
#include <cJSON.h>

#define JSON_PATH "./appconfig.json"
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
int WriteConfig(const char *old_str, const char *new_str);
void cleanup();

ipc_client_t ipcClient = {.fd = -1};
volatile sig_atomic_t g_running = 1;
int g_server_fd = -1;
int g_client_fd = -1;

/// @brief 退出回调
/// @param sig
void handle_sig(int sig)
{
    log_info("Get Signal %d\n", sig);
    log_info("cleaning");
    g_running = 0;
    // 打断 accept()
    if (g_server_fd >= 0)
    {
        close(g_server_fd);
    }
    if (g_client_fd >= 0)
    {
        close(g_client_fd);
    }
    cleanup();
    exit(0);
}

int main(int argc, char *argv[])
{
    // 捕获退出信号
    signal(SIGINT, handle_sig);  // Ctrl+C
    signal(SIGTERM, handle_sig); // kill

    // TODO：Progress Mutex
    int initMutexStatus = CheckMutex();
    // init TCP Server
    g_server_fd = InitTcpServer(PORT);
    if (g_server_fd == -1)
    {
        log_error("Start TCP Server Failed.");
        return 1;
    }
    log_info("Server listening on %d...\n", PORT);

    struct sockaddr_in clientAddress;
    socklen_t clientLen = sizeof(clientAddress);
    char buffer[BUF_SIZE];
    bool reinitIPC = true;
    ipcClient.fd = -1;
    if (InitIPC(&ipcClient) != 0)
    {
        log_error("Init IPC Failed");
    }
    // 使用-2 不显示消息 防止多次初始化IPC多次提示
    switch (initMutexStatus)
    {
    case 0:
        initMutexStatus = -2;
        notice_with_warning(&ipcClient, "已启动IPC", "不建议在IPC正常工作时再次启动\n仅建议在无应答时再次运行以重启");
        WriteConfig("1", "2");
        break;
    case 1:
        initMutexStatus = -2;
        notice_with_warning(&ipcClient, "已重新启动IPC", "已击杀之前的IPC进程\n仅建议在无应答时再次运行以重启");
        break;
    default:
        break;
    }
    // 主TCP循环
    while (g_running)
    {
        // init TCP
        int tcpClientFD = accept(g_server_fd,
                                 (struct sockaddr *)&clientAddress,
                                 &clientLen);
        if (tcpClientFD < 0)
        {
            perror("accept");
            continue;
        }
        log_info("Client: %s:%d\n",
                 inet_ntoa(clientAddress.sin_addr),
                 ntohs(clientAddress.sin_port));
        while (g_running)
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
            }
            n = read(tcpClientFD, buffer, BUF_SIZE - 1);
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

        close(tcpClientFD);
        log_info("Client disconnected\n");
    }
    cleanup();
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

/// @brief 检测是否有旧进程存在
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

    int returnCode = 0;
    // 多个
    if (count > 1)
    {
        pid_t mypid = getpid();
        for (int i = 0; i < count; i++)
        {
            if (mypid == pidList[i])
            {
                continue;
            }
            if (kill(pidList[i], SIGINT) == 0)
            {
                log_info("Old ipc process terminated\n");

                // 不覆盖失败
                if (returnCode != -1)
                {
                    returnCode = 1;
                }
            }
            else
            {
                log_error("Terminate old ipc process FAILED\n");
                returnCode = -1;
            }
        }
    }
    // 就tm一个
    else
    {
        returnCode = 0;
    }
    closedir(dir);
    return returnCode;
}

int WriteConfig(const char *old_str, const char *new_str)
{
    FILE *fp = fopen(JSON_PATH, "r");
    if (!fp)
    {
        perror("fopen");
        return -1;
    }

    // 读取文件
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *data = malloc(size + 1);
    if (data == NULL)
    {
        log_error("malloc Failed!");
        return;
    }
    fread(data, 1, size, fp);
    data[size] = '\0';
    fclose(fp);

    // 解析 JSON
    cJSON *root = cJSON_Parse(data);
    if (!root)
    {
        log_error("JSON解析失败\n");
        free(data);
        return -1;
    }

    // 获取 description
    cJSON *desc = cJSON_GetObjectItem(root, "description");
    if (!desc || !cJSON_IsString(desc))
    {
        log_info("description字段不存在或类型错误\n");
        cJSON_Delete(root);
        free(data);
        return -1;
    }

    // 修改值
    cJSON_SetValuestring(desc, "当前网络IPC已启动");

    // 重新生成 JSON 字符串
    char *new_json = cJSON_Print(root);

    // 写回文件
    fp = fopen(JSON_PATH, "w");
    if (!fp)
    {
        perror("fopen");
        cJSON_Delete(root);
        free(data);
        free(new_json);
        return -1;
    }

    fputs(new_json, fp);
    fclose(fp);

    // 释放
    cJSON_Delete(root);
    free(data);
    free(new_json);

    log_info("修改完成\n");
    return 0;
}

void cleanup()
{
    log_info("Cleaning resources...");

    // 关闭 client
    if (g_client_fd >= 0)
    {
        shutdown(g_client_fd, SHUT_RDWR);
        close(g_client_fd);
        g_client_fd = -1;
    }

    // 关闭 server
    if (g_server_fd >= 0)
    {
        close(g_server_fd);
        g_server_fd = -1;
    }

    // 销毁 IPC
    if (ipcClient.fd >= 0)
    {
        ipc_client_destroy(&ipcClient);
        ipcClient.fd = -1;
    }

    log_info("Cleanup done.");
}
