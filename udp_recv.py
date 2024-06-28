import socket

def main():
    # 监听地址和端口
    listen_host = '127.0.0.1'
    listen_port = 27015  # port

    # 创建UDP套接字
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # 绑定地址和端口
    server_socket.bind((listen_host, listen_port))
    print(f"Listening on {listen_host}:{listen_port}...")

    try:
        while True:
            # 接收数据
            data, addr = server_socket.recvfrom(1024)
            print(f"Received {len(data)} bytes from {addr}:")
            print(data.decode())

    except KeyboardInterrupt:
        print("\nUser interrupted. Exiting...")

    finally:
        # 关闭套接字
        server_socket.close()

if __name__ == "__main__":
    main()
