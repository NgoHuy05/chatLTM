#include "protocol.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <atomic>
#include <ctime>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")

SOCKET sock;
std::atomic<bool> running(true);
uint32_t g_msgId = 1;
std::unordered_map<uint32_t, std::string> sent_files;

/* ================= UTILS ================= */
uint32_t checksum(const uint8_t *d, size_t n) {
    uint32_t c = 0;
    for (size_t i = 0; i < n; i++)
        c ^= d[i];
    return c;
}

/* ================= GAME STATE ================= */
std::vector<char> board(9, ' ');
bool inGame = false;
bool myTurn = false;
char me = 'X', other = 'O';

void draw_board() {
    system("cls");
    for (int i = 0; i < 9; i++) {
        std::cout << " " << (board[i] == ' ' ? char('0' + i) : board[i]) << " ";
        if (i % 3 != 2) std::cout << "|";
        if (i % 3 == 2 && i != 8) std::cout << "\n-----------\n";
    }
    std::cout << "\n";
}

bool win(char p) {
    int w[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    for (auto &x : w)
        if (board[x[0]]==p && board[x[1]]==p && board[x[2]]==p)
            return true;
    return false;
}

/* ================= SOCKET HELPERS ================= */
bool send_all(const void *d, size_t n) {
    const char *p = (const char*)d;
    while(n) {
        int s = send(sock, p, (int)n, 0);
        if(s <= 0) return false;
        p += s; n -= s;
    }
    return true;
}

bool recv_all(void *d, size_t n) {
    char *p = (char*)d;
    while(n) {
        int r = recv(sock, p, (int)n, 0);
        if(r <= 0) return false;
        p += r; n -= r;
    }
    return true;
}

/* ================= PACKET ================= */
void send_packet(uint32_t type, const std::string &sender, const std::string &topic, uint8_t flags,
                 const std::vector<uint8_t> &payload, uint32_t msgId=0)
{
    PacketHeader h{};
    h.msgType = type;
    h.payloadLength = (uint32_t)payload.size();
    h.messageId = msgId ? msgId : g_msgId++;
    h.timestamp = (uint32_t)time(nullptr);
    h.version = PROTOCOL_VERSION;
    h.flags = flags;
    strncpy(h.sender, sender.c_str(), MAX_USERNAME_LEN-1);
    strncpy(h.topic, topic.c_str(), MAX_TOPIC_LEN-1);
    if(!payload.empty())
        h.checksum = checksum(payload.data(), payload.size());

    send_all(&h, sizeof(h));
    if(!payload.empty())
        send_all(payload.data(), payload.size());
}

/* ================= FILE PICKER ================= */
std::wstring pick_file() {
    OPENFILENAMEW ofn{};
    wchar_t file[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if(GetOpenFileNameW(&ofn)) return file;
    return L"";
}

/* ================= RECEIVE LOOP ================= */
struct IncomingFile {
    std::ofstream ofs;
    std::string filename;
    bool opened = false;
};
std::unordered_map<uint32_t, IncomingFile> open_files;

void recv_loop() {
std::filesystem::create_directory("client_upload");

    while(running) {
        PacketHeader h{};
        if(!recv_all(&h, sizeof(h))) break;

        std::vector<uint8_t> payload(h.payloadLength);
        if(h.payloadLength)
            recv_all(payload.data(), payload.size());

        switch(h.msgType) {
            case MSG_PUBLISH_TEXT: {
                std::string topic = h.topic;

                if(topic=="/sys/user_list") {
                    std::cout << "\n=== USER ONLINE ===\n";
                    std::cout.write((char*)payload.data(), payload.size());
                    std::cout << "\n===================\n";
                    break;
                }

                // Game logic
                if(topic=="/game/start") {
                    me = payload[0]; other = (me=='X')?'O':'X';
                    board.assign(9,' '); inGame=true; myTurn=(me=='X');
                    draw_board();
                    std::cout << "Game started. You are " << me << "\n";
                    if(myTurn) std::cout << "Your turn. Enter 5 to move: \n";
                    break;
                }
                if(topic=="/game/wait") { std::cout << "Dang cho doi thu...\n"; break; }
                if(topic=="/game/reject") { std::cout << "Game dang ban\n"; break; }
                if(topic=="/game/move") {
                    if(!inGame) break;
                    int move; memcpy(&move,payload.data(),sizeof(int));
                    if(move<0 || move>8 || board[move]!=' ') break;
                    board[move]=other; draw_board();
                    if(win(other)) { std::cout << "YOU LOSE\n"; inGame=false; break; }
                    myTurn=true; std::cout << "Your turn. Enter 5 to move\n";
                    break;
                }

                // Chat message
                std::cout << "\n[" << h.sender
                          << (h.flags & FLAG_PRIVATE ? " -> " : " -> ") 
                          << h.topic << "] ";
                std::cout.write((char*)payload.data(), payload.size());
                std::cout << "\n";
                break;
            }

            case MSG_PUBLISH_FILE: {
                std::string fname((char*)payload.data(), payload.size());
                IncomingFile f; f.filename="client_upload/"+fname; f.ofs.open(f.filename,std::ios::binary); f.opened=true;
                open_files[h.messageId]=std::move(f);
                std::cout << "\n[RECEIVING FILE] " << fname << "\n";
                break;
            }

            case MSG_FILE_DATA: {
                auto it=open_files.find(h.messageId);
                if(it==open_files.end()) break;
                if(!payload.empty()) it->second.ofs.write((char*)payload.data(), payload.size());
                if(h.flags & FLAG_LAST) {
                    it->second.ofs.close();
                    std::cout << "[FILE SAVED] " << it->second.filename << "\n";
                    open_files.erase(it);
                }
                break;
            }

            case MSG_ERROR: {
                std::cout << "\n[ERROR] ";
                std::cout.write((char*)payload.data(), payload.size());
                std::cout << "\n";
                break;
            }

            default: break;
        }
    }
}

void send_file(const std::string &user, const std::string &target, bool priv) {
    std::wstring path = pick_file();
    if(path.empty()) return;

    std::string filename = std::filesystem::path(path).filename().string();
    uint32_t msgId = g_msgId++;
    sent_files[msgId] = target;

    // 1. Gửi tên file
    send_packet(MSG_PUBLISH_FILE, user, target, priv ? FLAG_PRIVATE : FLAG_GROUP,
                std::vector<uint8_t>(filename.begin(), filename.end()), msgId);

    // 2. Gửi dữ liệu file
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE) { std::cout << "Cannot open file\n"; return; }

    const DWORD BUF = 1024;
    std::vector<uint8_t> buf(BUF);
    DWORD read = 0;
    while(ReadFile(hFile, buf.data(), BUF, &read, NULL) && read > 0) {
        buf.resize(read);
        send_packet(MSG_FILE_DATA, user, target, priv ? FLAG_PRIVATE : FLAG_GROUP, buf, msgId);
        buf.resize(BUF);
    }
    CloseHandle(hFile);

    // 3. Gửi flag LAST
    send_packet(MSG_FILE_DATA, user, target, (priv ? FLAG_PRIVATE : FLAG_GROUP) | FLAG_LAST, {}, msgId);

    // 4. Gửi thông báo tới người nhận
    std::string notifyMsg = "[FILE SENT] " + filename + "\n";
    send_packet(MSG_PUBLISH_TEXT, user, target, priv ? FLAG_PRIVATE : FLAG_GROUP,
                std::vector<uint8_t>(notifyMsg.begin(), notifyMsg.end()));

    std::cout << "[FILE SENT] " << filename << "\n";
}

/* ================= MAIN ================= */
int main() {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);

    sock=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(DEFAULT_PORT);
    inet_pton(AF_INET,"10.11.192.187",&addr.sin_addr);

    if(connect(sock,(sockaddr*)&addr,sizeof(addr))!=0) { std::cout<<"Connect failed\n"; return 1; }

    std::string user;
    std::cout<<"Username: "; std::getline(std::cin,user);
    send_packet(MSG_LOGIN,user,"",0,{});

    std::thread recvThread(recv_loop);

    while(running) {
        if(inGame && myTurn) {
            std::string input; int pos=-1;
            while(true) {
                std::cout<<"Your turn! Enter 0-8: "; std::getline(std::cin,input);
                try { pos=std::stoi(input); } catch(...) { std::cout<<"Invalid number\n"; continue; }
                if(pos<0||pos>8||board[pos]!=' ') { std::cout<<"Invalid position\n"; continue; }
                break;
            }
            board[pos]=me; draw_board();
            if(win(me)) { std::cout<<"YOU WIN\n"; inGame=false; }
            send_packet(MSG_PUBLISH_TEXT,user,"/game/move",FLAG_GROUP,
                        std::vector<uint8_t>((uint8_t*)&pos,(uint8_t*)&pos+sizeof(int)));
            myTurn=false; continue;
        }

        std::cout<<"\n|1: Group|2: Messenger|3: Lists|4: Game|5: Move|6: Quit|\nChoose: ";
        std::string input; std::getline(std::cin,input);
        int choice=-1; try { choice=std::stoi(input); } catch(...) { std::cout<<"Invalid choice\n"; continue; }

        switch(choice) {
            case 1: {
                std::cout<<"\n=== GROUP MENU ===\n1: Subscribe\n2: Unsubscribe\n0: Back\nChoose: ";
                std::getline(std::cin,input); int gchoice=-1; try { gchoice=std::stoi(input); } catch(...) { std::cout<<"Invalid\n"; break; }
                if(gchoice==0) break;
                std::cout<<"Topic: "; std::string topic; std::getline(std::cin,topic);
                if(gchoice==1) send_packet(MSG_SUBSCRIBE,user,topic,0,{});
                else if(gchoice==2) send_packet(MSG_UNSUBSCRIBE,user,topic,0,{});
                else std::cout<<"Invalid choice\n";
                break;
            }

            case 2: {
                std::cout<<"\n=== MESSENGER MENU ===\n1: Msg\n2: PM\n3: File\n0: Back\nChoose: ";
                std::getline(std::cin,input); int mchoice=-1; try { mchoice=std::stoi(input); } catch(...) { std::cout<<"Invalid\n"; break; }
                if(mchoice==0) break;

                if(mchoice==1) { std::string topic,msg; std::cout<<"Topic: "; std::getline(std::cin,topic); std::cout<<"Msg: "; std::getline(std::cin,msg);
                                 send_packet(MSG_PUBLISH_TEXT,user,topic,FLAG_GROUP,std::vector<uint8_t>(msg.begin(),msg.end())); }
                else if(mchoice==2) { std::string target,msg; std::cout<<"User: "; std::getline(std::cin,target); std::cout<<"Msg: "; std::getline(std::cin,msg);
                                      send_packet(MSG_PUBLISH_TEXT,user,target,FLAG_PRIVATE,std::vector<uint8_t>(msg.begin(),msg.end())); }
                else if(mchoice==3) {
                    std::cout<<"Send file to: 1. User 2. Topic 0. Back\nChoose: ";
                    std::getline(std::cin,input); int fchoice=-1; try{fchoice=std::stoi(input);}catch(...){break;}
                    if(fchoice==0) break;
                    std::string target; std::cout<<(fchoice==1?"User: ":"Topic: "); std::getline(std::cin,target);
                    send_file(user,target,fchoice==1);
                }
                else std::cout<<"Invalid choice\n";
                break;
            }

            case 3: {
                std::cout<<"\n=== LISTS MENU ===\n1: Users\n2: Topics\n0: Back\nChoose: ";
                std::getline(std::cin,input); int lchoice=-1; try{lchoice=std::stoi(input);}catch(...){std::cout<<"Invalid\n"; break;}
                if(lchoice==0) break;
                if(lchoice==1) { std::ifstream ifs("online.txt"); if(!ifs){std::cout<<"Cannot open online.txt\n"; break;}
                                 std::cout<<"\n=== USER ONLINE ===\n"; bool empty=true; for(std::string line; std::getline(ifs,line);){ std::cout<<line<<"\n"; empty=false; }
                                 if(empty) std::cout<<"(No users online)\n"; std::cout<<"====================\n"; }
                else if(lchoice==2) { std::ifstream ifs("topics.txt"); if(!ifs){std::cout<<"Cannot open topics.txt\n"; break;}
                                     std::cout<<"\n=== TOPICS ===\n"; bool empty=true; for(std::string line; std::getline(ifs,line);){ std::cout<<line<<"\n"; empty=false; }
                                     if(empty) std::cout<<"(No topics)\n"; std::cout<<"====================\n"; }
                else std::cout<<"Invalid choice\n";
                break;
            }

            case 4: if(inGame) std::cout<<"Ban dang trong game\n"; else { send_packet(MSG_PUBLISH_TEXT,user,"/game/join",FLAG_GROUP,{}); std::cout<<"Dang tim doi thu...\n"; } break;
            case 5: if(!inGame) std::cout<<"Chua vao game\n"; else if(!myTurn) std::cout<<"Chua toi luot\n"; else { std::cout<<"Your turn. Enter 0-8: "; std::getline(std::cin,input); int pos=-1; try{pos=std::stoi(input);}catch(...){break;}
                        if(pos<0||pos>8||board[pos]!=' ') { std::cout<<"Invalid position\n"; break; }
                        board[pos]=me; draw_board(); if(win(me)){ std::cout<<"YOU WIN\n"; inGame=false; }
                        myTurn=false; send_packet(MSG_PUBLISH_TEXT,user,"/game/move",FLAG_GROUP,std::vector<uint8_t>((uint8_t*)&pos,(uint8_t*)&pos+sizeof(int))); } break;
            case 6: send_packet(MSG_LOGOUT,user,"",0,{}); running=false; closesocket(sock); WSACleanup(); recvThread.join(); return 0;
            default: std::cout<<"Invalid choice\n"; break;
        }
    }

    running=false; closesocket(sock); WSACleanup(); recvThread.join();
    return 0;
}
