# Proxy2Proxy


## Introduction
You can use it to build a tunnel in between local service and public server (like NAT). 
- first_proxy: The public server constructs a proxy, providing a public IP address and port, which can be accessed by the local service.
- second_proxy: The local service can be a web server, a game server, or any other service, which usually runs in the local network(LAN).

可以使用它在本地服务和公网服务器之间建立一个隧道（类似NAT）。

- first_proxy：公网服务器构建代理，提供公网IP地址和端口，可以被本地服务访问。
- second_proxy：本地服务可以是web服务器、游戏服务器或其他任何服务，通常运行在本地网络（LAN）中。


## Implementation

I start the proxy service on the public server first. Then the local service builds the link and the public server. When the client requests service to the public server, the public tells the local server to build the bi-directional link.

我在公网首先构建代理服务，然后本地服务与公网建立控制连接。当客户端向公网请求服务时，公网通过控制链接告诉本地服务，本地服务向公网构建新链接形成公网的双向链接。随后双工转发消息即可。


## Structure of the Source Code
```
second_proxy (developed in the windows)
├── developed proxy in real proxy mechine(such as CLASH)
├── io.cpp: network I/O
├── third_party: just wepoll, used as eventlopp in the windows
├── threadpool.hpp: multi-thread handles message transfer
├── main.cpp: main function
first_proxy (developed in the public server)
├── developed proxy in public server, which you need to set as the proxy
├── io.cpp: network I/O
├── threadpool.hpp: multi-thread handles message transfer
├── main.cpp: main function
```

## Usage
EngLish
- change configures in the main.cpp (both first_proxy and second_proxy)
- compile and run the first proxy in public server
- compile and run the second proxy in the local server
- set the proxy wheneve and wherever you want to use the local service 

中文
- 修改main.cpp中的配置(both first_proxy and second_proxy)
- 编译并运行公网的first_proxy
- 编译并运行本地的second_proxy
- 在外工作时，设置公网代理(first proxy)使用本地服务