/*
 *  此处用于连接 Drm_neo_app 与 ESP32
 *
 *
 *   串口数据帧格式：
 *   +--------------+--------------+------------------------------------------------------------------------------+-------------+-------------+--------------+--------------+
 *   |    HEAD 0    |    HEAD 1    |                                  (BODY)                                      |    CRC 0    |    CRC 1    |    TAIL 0    |    TAIL 1    |
 *   |     0xAA     |     0x55     |                                  (BODY)                                      |     CRC     |     CRC     |     0x0D     |     0x07     |
 *   +--------------+--------------+------------------------------------------------------------------------------+-------------+-------------+--------------+--------------+
 *                                 +------------------+------------------+-------------------+--------------------+
 *                                 |    Package ID    |  Operation Code  |  Operation size   |   Operation Data   |
 *                                 |      uint 32     |      uint 8      |      uint 16      |    unsigned char   |
 *                                 +------------------+------------------+-------------------+--------------------+
 *                                       Reserved                                  |             512 Bytes (Max)
 *                                 ^                                               |                    ^         ^
 *                                 |                                               +--------------------+         |
 *                                 +-------------------------------CRC Protected----------------------------------+
 *  Operation Code 见枚举
 */

// 此枚举在esp-now和UART中通用
typedef enum
{
    CONTOL_BRIGHTNESS = 128, // Operation Data 为一个int 代表目标屏幕亮度
    CONTOL_EXIT,             // 退出Drm_App

} OPERATION_CODE_ENUM;


#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
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
#include <termios.h>
#include <stdint.h>

#pragma pack(push, 1) // 紧凑 1字节对齐
typedef struct
{
    uint32_t packageID;
    uint8_t OperationCode;
    uint16_t OperationSize;
    uint8_t OperationData[512];
} BodyDef_t;
#pragma pack(pop)  

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
void cleanup();
int InitSerial(int *fd, const char *dev, int baudrate);
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

    int initMutexStatus = CheckMutex();

    // 启动IPC
    bool reinitIPC = true;
    if (InitIPC(&ipcClient) != 0)
    {
        log_error("Init IPC Failed");
    }

    // 启动UART
    int uartFd = 0;
    if (InitSerial(&uartFd, "/dev/ttyS1", 115200) != 0)
    {
        log_error("open UART failed");
        notice_with_warning(&ipcClient, "启动uart1失败", "请检查是否有其他应用程式占用 \n或是否在srgn_config中启动uart1");
        return -1;
    }

    // 脱离控制
    pid_t parentPid = getpid();
    log_info("Parent PID = %d\n", parentPid);
    fork();
    pid_t childrenPid = getpid();
    log_info("Children PID = %d\n", childrenPid);
    if (childrenPid == parentPid)
    {
        log_info("SELF KILLED! PID = %d\n", childrenPid);
        return 0;
    }

    // 后台低优先级
    if (setpriority(PRIO_PROCESS, 0, 10) == -1)
    {
        log_error("setpriority Failed");
    }
    int prio = getpriority(PRIO_PROCESS, 0);
    log_info("Current nice: %d\n", prio);

    // 使用-2 不显示消息 防止多次初始化IPC多次提示
    switch (initMutexStatus)
    {
    case 0:
        initMutexStatus = -2;
        notice_with_warning(&ipcClient, "已启动IPC和后台保活", "不建议在IPC正常工作时再次启动\n仅建议在无应答时再次运行以重启");
        break;
    case 1:
        initMutexStatus = -2;
        notice_with_warning(&ipcClient, "已重新启动IPC", "已击杀之前的IPC进程\n仅建议在无应答时再次运行以重启");
        break;
    default:
        break;
    }

    // 主UART循环
    while (g_running)
    {
        char buf[128];
        int n = read(uartFd, buf, sizeof(buf));

        if (n > 0)
        {
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
            int success = MessageProcesser(buf, &ipcClient);
            if (success != 0)
            {
                reinitIPC = true;
                continue;
            }
        }
        else if (n == 0)
        {
            // timeout
            continue;
        }
        else
        {
            perror("read");
            break;
        }
    }
    cleanup();
    return 0;
}

int InitSerial(int *fd, const char *dev, int baudrate)
{
    if (!fd || !dev)
        return -1;

    // 打开串口（阻塞）
    int tmpfd = open(dev, O_RDWR | O_NOCTTY);
    if (tmpfd < 0)
    {
        perror("open");
        return -1;
    }

    struct termios opt;
    if (tcgetattr(tmpfd, &opt) != 0)
    {
        perror("tcgetattr");
        close(tmpfd);
        return -1;
    }

    // 设置波特率
    speed_t speed;
    switch (baudrate)
    {
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    default:
        fprintf(stderr, "unsupported baudrate\n");
        close(tmpfd);
        return -1;
    }

    cfsetispeed(&opt, speed);
    cfsetospeed(&opt, speed);

    // 8N1
    opt.c_cflag |= (CLOCAL | CREAD);
    opt.c_cflag &= ~CSIZE;
    opt.c_cflag |= CS8;
    opt.c_cflag &= ~PARENB;
    opt.c_cflag &= ~CSTOPB;

    // 原始模式
    opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    opt.c_iflag &= ~(IXON | IXOFF | IXANY);
    opt.c_oflag &= ~OPOST;

    // 关键：1秒超时
    opt.c_cc[VMIN] = 0;   // 不要求最少字节
    opt.c_cc[VTIME] = 10; // 1 秒（单位 0.1s）

    if (tcsetattr(tmpfd, TCSANOW, &opt) != 0)
    {
        perror("tcsetattr");
        close(tmpfd);
        return -1;
    }

    *fd = tmpfd;
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

                if (strcmp(name, "UartController") == 0)
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
