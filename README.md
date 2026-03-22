# ipc_client_demo

IPC 客户端示例，覆盖 `ipc_common.h` 中的所有请求类型。

## 说明
- 启动后会依次调用所有 IPC 功能并打印结果日志。
- `app_exit` 会主动退出上层应用，默认不发送，可用 `--send-exit` 开启。
- 示例依赖的测试图片/视频会通过 CMake `install` 被安装到 `assets/` 目录。
