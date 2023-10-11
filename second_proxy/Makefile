CFLAGS = -std=c++11 \
 -I E:/gitProject/proxy2proxy_mine_latest/second_proxy/third_party/wepoll 
LINKS = -lws2_32  -lwsock32 -lpthread

# 源文件
SRCS = main.cpp io.cpp threadpool.hpp
C_SRCS = E:/gitProject/proxy2proxy_mine_latest/second_proxy/third_party/wepoll/wepoll.c

wepoll:
	gcc -c $(C_SRCS) -o wepoll.o


main: wepoll
	g++ -c main.cpp -o main.o $(CFLAGS)
	g++ -c io.cpp -o io.o $(CFLAGS)
	g++ main.o io.o wepoll.o -o main.exe $(LINKS)  -static-libgcc -static-libstdc++