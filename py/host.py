import socket
import struct

SERVER_IP = "192.168.137.2"
SERVER_PORT = 1572
BUF_SIZE = 1024  # 最大包长度

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((SERVER_IP, SERVER_PORT))
    print("Connected to {}:{}".format(SERVER_IP, SERVER_PORT))

    while True:
        try:
            # 输入操作码
            op_code = int(input("Enter Operation Code (0-255): "))
            if op_code < 0 or op_code > 255:
                print("Invalid op code")
                continue

            # 输入操作数据（整数）
            op_data = input("Enter Operation Data (integer): ")
            if op_data == "":
                continue
            op_int = int(op_data)

            # 打包：Operation Code (1 byte) + Operation Size (2 bytes) + Operation Data (4 bytes)
            op_size = 4  # int32 固定 4 字节
            packet = struct.pack("<B H i", op_code, op_size, op_int)  # little-endian
            if len(packet) > BUF_SIZE:
                print("Packet too large, skip")
                continue

            # 发送
            s.sendall(packet)
            print("Sent packet: Operation Code={}, Data={}".format(op_code, op_int))

        except Exception as e:
            print("Error:", e)
            break