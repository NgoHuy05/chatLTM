truoc khi chay duoc thi phai sua ip ket noi

sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(DEFAULT_PORT);
inet_pton(AF_INET,"192.168.1.13",&addr.sin_addr);

window R cmd
-> ipconfig
tim IPv4 thay cai dia chi cia IPv4 vao vd inet_pton(AF_INET,"..........",&addr.sin_addr);

build file server va client de ra file exe go 2 lenh sau
g++ client.cpp -o client.exe -lws2_32 -lcomdlg32 -pthread
g++ server.cpp mongoose.c -o server.exe -lws2_32 -pthread

sau khi co file exe thi chay server.exe truoc sau do mo 2 terminal chay 2 lan client.exe de test tinh nang
.\server.exe
.\client.exe


