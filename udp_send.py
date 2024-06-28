import socket

def main():
    # 目标地址和端口
    target_host = '172.16.100.204'
    target_port = 8906

    # 创建UDP套接字
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # 绑定本地端口（可选）
    # client_socket.bind(('127.0.0.1', 3461))

    # 要发送的数据
    message = "Hello, UDP 172!"

    try:
        # 发送数据到指定地址和端口
        client_socket.sendto(message.encode(), (target_host, target_port))
        print(f"Sent message to {target_host}:{target_port}")

    except Exception as e:
        print(f"Error while sending data: {e}")

    finally:
        # 关闭套接字
        client_socket.close()

if __name__ == "__main__":
    main()
