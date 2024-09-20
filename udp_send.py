# this python script only for test
import socket
import time

def main():
    # 目标地址和端口
    target_host = '127.0.0.1'
    target_port = 3460

    # 创建UDP套接字
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # 绑定本地端口（可选）
    client_socket.bind(('127.0.0.1', 27015))

    # 要发送的数据
    message = ""
    for _ in range(11):  # max 65507
        message += "s"
    while True:
        client_socket.sendto(message.encode(), (target_host, target_port))
        time.sleep(0.002)


if __name__ == "__main__":
    main()
