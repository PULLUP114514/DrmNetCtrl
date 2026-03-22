import socket

SERVER_IP = "192.168.137.2"
SERVER_PORT = 1572

# 创建 TCP socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    try:
        s.connect((SERVER_IP, SERVER_PORT))
        print("Connected to {}:{}".format(SERVER_IP, SERVER_PORT))

        while True:
            # 从控制台读取输入
            message = input("Input message to send: ")
            if message.lower() in ("exit", "quit"):
                break

            # 发送
            s.sendall(message.encode())
            print("Sent: {}".format(message))

            # 可选择接收服务器响应
            data = s.recv(1024)
            print("Received:", data.decode())

    except Exception as e:
        print("Error:", e)