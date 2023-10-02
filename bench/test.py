import http.client

# 代理服务器的地址和端口
proxy_host = "127.0.0.1"
proxy_port = 12345

# 目标网站的主机和端口
target_host = "www.baidu.com"
target_port = 443

# 创建HTTP连接对象
conn = http.client.HTTPConnection(proxy_host, proxy_port)

# 发送CONNECT请求
conn.request("CONNECT", target_host + ":" + str(target_port))

# 获取响应
response = conn.getresponse()

if response.status == 200:
    print("成功建立隧道连接到目标网站")
else:
    print(f"连接失败，状态码: {response.status}, 响应消息: {response.reason}")

# 在此之后，您可以使用conn对象来发送HTTPS请求，如：
# conn.request("GET", "/")
# response = conn.getresponse()
# data = response.read()
# print(data)

# 最后关闭连接
conn.close()
