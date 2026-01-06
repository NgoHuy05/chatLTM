// ================= SERVER.CPP =================
// Server chat TCP + WebSocket hỗ trợ:
// - Login/Logout
// - Subscribe/Unsubscribe topic
// - Publish text/message
// - Publish file (chunked)
// - Forward game moves giữa 2 client
// =================================================

#include "protocol.h"
#include "mongoose.h"

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <fstream>
#include <ctime>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

// ---------------- CONFIG ----------------
#define TCP_PORT "tcp://0.0.0.0:8080"
#define WS_PORT "http://0.0.0.0:8000"

const std::string ONLINE_FILE = "online.txt";          // danh sách user online
const std::string TOPICS_FILE = "topics.txt";          // danh sách topic
const std::string USER_TOPIC_FILE = "user_topics.txt"; // mapping username:topic

// ---------------- CLIENT STRUCT ----------------
struct Client
{
    std::string username;                   // username
    std::unordered_set<std::string> topics; // topic đã subscribe
    bool is_ws = false;                     // true nếu client kết nối WS
};

// ---------------- FILE STRUCT ----------------
struct IncomingFile
{
    std::ofstream ofs;  // file đang ghi
    std::string sender; // người gửi
    std::string target; // username hoặc topic
    bool is_private = false;
    std::string filename; // tên file gốc
};

// ---------------- GAME STRUCT ----------------
struct GameRoom
{
    mg_connection *p1 = nullptr; // player 1
    mg_connection *p2 = nullptr; // player 2
    bool started = false;        // trạng thái game
};

// ---------------- GLOBALS ----------------
static std::unordered_set<std::string> onlineUsers;           // danh sách user online
static std::unordered_map<mg_connection *, Client> g_clients; // map connection -> client
static std::unordered_map<uint32_t, IncomingFile> g_files;    // messageId -> file transfer
static GameRoom g_game;                                       // game 1 vs 1
static std::mutex g_mu;                                       // mutex bảo vệ các map

// ---------------- UTILS ----------------

// Tính checksum XOR của payload
uint32_t calc_checksum(const uint8_t *data, size_t n)
{
    uint32_t c = 0;
    for (size_t i = 0; i < n; i++)
        c ^= data[i];
    return c;
}

// Gửi packet theo protocol
void send_packet(mg_connection *c, PacketHeader &h, const void *payload)
{
    // tính checksum
    h.checksum = (payload && h.payloadLength) ? calc_checksum((const uint8_t *)payload, h.payloadLength) : 0;

    if (g_clients[c].is_ws)
    {
        // WebSocket: gửi PacketHeader + payload tách riêng
        mg_ws_send(c, &h, sizeof(h), WEBSOCKET_OP_BINARY);
        if (payload && h.payloadLength)
            mg_ws_send(c, payload, h.payloadLength, WEBSOCKET_OP_BINARY);
    }
    else
    {
        // TCP: gửi liên tiếp
        mg_send(c, &h, sizeof(h));
        if (payload && h.payloadLength)
            mg_send(c, payload, h.payloadLength);
    }
}

// Gửi ACK theo messageId
void send_ack(mg_connection *c, uint32_t msgId)
{
    PacketHeader h{};
    h.msgType = MSG_ACK;
    h.messageId = msgId;
    h.timestamp = time(nullptr);
    h.version = PROTOCOL_VERSION;
    send_packet(c, h, nullptr);
}

// Gửi lỗi kèm msg
void send_error(mg_connection *c, uint32_t msgId, const char *msg)
{
    PacketHeader h{};
    h.msgType = MSG_ERROR;
    h.payloadLength = strlen(msg);
    h.messageId = msgId;
    h.timestamp = time(nullptr);
    h.version = PROTOCOL_VERSION;
    send_packet(c, h, msg);
}

// Kiểm tra user online
bool user_online(const std::string &username)
{
    return onlineUsers.count(username) > 0;
}

// Kiểm tra topic có subscriber
bool topic_has_subscribers(const std::string &topic)
{
    for (auto &[_, cli] : g_clients)
        if (cli.topics.count(topic))
            return true;
    return false;
}

// Gửi text game/private/topic
void send_game_text(mg_connection *c, const std::string &topic, const std::string &text)
{
    PacketHeader h{};
    h.msgType = MSG_PUBLISH_TEXT;
    h.payloadLength = (uint32_t)text.size();
    h.timestamp = time(nullptr);
    h.version = PROTOCOL_VERSION;
    strncpy(h.topic, topic.c_str(), MAX_TOPIC_LEN - 1);
    send_packet(c, h, text.data());
}

// Gửi private message
void send_private(const std::string &user, PacketHeader &h, const void *payload)
{
    for (auto &[c, cli] : g_clients)
        if (cli.username == user)
            send_packet(c, h, payload);
}

// Broadcast tới tất cả subscriber của topic (ngoại trừ sender)
void broadcast_topic(const std::string &topic, PacketHeader &h, const void *payload, mg_connection *src)
{
    for (auto &[c, cli] : g_clients)
    {
        if (c == src)
            continue;
        if (cli.topics.count(topic))
            send_packet(c, h, payload);
    }
}

// ---------------- GAME HANDLER ----------------

// Server KHÔNG giữ board, chỉ forward /game/* cho đúng người
void handle_game(mg_connection *c, PacketHeader &h, const uint8_t *payload)
{
    std::string topic = h.topic;

    // ==== JOIN ====
    if (topic == "/game/join")
    {
        if (g_game.started)
        {
            send_game_text(c, "/game/reject", "room_full");
            return;
        }

        if (!g_game.p1)
        {
            g_game.p1 = c;
            send_game_text(c, "/game/wait", "waiting_player");
            return;
        }

        if (!g_game.p2 && c != g_game.p1)
        {
            g_game.p2 = c;
            g_game.started = true;
            send_game_text(g_game.p1, "/game/start", "X");
            send_game_text(g_game.p2, "/game/start", "O");
            return;
        }
        return;
    }

    // ==== MOVE ====
    if (topic == "/game/move")
    {
        if (!g_game.started)
            return;

        mg_connection *other = (c == g_game.p1) ? g_game.p2 : (c == g_game.p2) ? g_game.p1
                                                                               : nullptr;
        if (!other)
            return;

        // Forward move
        send_packet(other, h, payload);
    }
}

// ---------------- PACKET HANDLER ----------------
void handle_packet(mg_connection *c, PacketHeader &h, const uint8_t *payload)
{
    std::lock_guard<std::mutex> lk(g_mu);
    Client &cli = g_clients[c];
    std::string topic_str(h.topic);

    switch (h.msgType)
    {

    case MSG_LOGIN:
        cli.username = h.sender;
        onlineUsers.insert(h.sender);

        // ghi online.txt
        {
            std::ofstream ofs(ONLINE_FILE, std::ios::app);
            ofs << h.sender << "\n";
        }
        send_ack(c, h.messageId);
        break;

    case MSG_LOGOUT:
        // game reset nếu đang chơi
        if (c == g_game.p1 || c == g_game.p2)
        {
            mg_connection *other = (c == g_game.p1) ? g_game.p2 : g_game.p1;
            if (other)
                send_game_text(other, "/game/abort", "opponent_left");
            g_game = GameRoom{};
        }

        if (!cli.username.empty())
        {
            std::string user = cli.username;

            // Xóa khỏi onlineUsers
            onlineUsers.erase(user);
            std::ofstream ofs(ONLINE_FILE, std::ios::trunc);
            for (auto &u : onlineUsers)
                ofs << u << "\n";

            // Xóa tất cả dòng user-topic của user
            std::ifstream ifs(USER_TOPIC_FILE);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(ifs, line))
            {
                if (line.find(user + ":") != 0)
                    lines.push_back(line); // giữ các dòng khác user
            }
            ifs.close();

            std::ofstream ofs2(USER_TOPIC_FILE, std::ios::trunc);
            for (auto &l : lines)
                ofs2 << l << "\n";
        }
        g_clients.erase(c);

        break;

    case MSG_SUBSCRIBE:
        cli.topics.insert(h.topic);
        send_ack(c, h.messageId);

        // ---- Lưu user-topic ----
        {
            std::unordered_set<std::string> existingUserTopics;
            std::ifstream ifs(USER_TOPIC_FILE);
            std::string line;
            while (std::getline(ifs, line))
                existingUserTopics.insert(line);
            ifs.close();

            std::string entry = cli.username + ":" + h.topic;
            if (existingUserTopics.find(entry) == existingUserTopics.end())
            {
                std::ofstream ofs(USER_TOPIC_FILE, std::ios::app);
                ofs << entry << "\n";
            }
        }

        // ---- Lưu topic chung ----
        {
            std::unordered_set<std::string> existingTopics;
            std::ifstream ifs(TOPICS_FILE);
            std::string tline;
            while (std::getline(ifs, tline))
                existingTopics.insert(tline);
            ifs.close();

            if (existingTopics.find(h.topic) == existingTopics.end())
            {
                std::ofstream ofs(TOPICS_FILE, std::ios::app);
                ofs << h.topic << "\n";
            }
        }
        break;

    case MSG_UNSUBSCRIBE:
        cli.topics.erase(h.topic);
        send_ack(c, h.messageId);

        // Xóa mapping khỏi file
        {
            std::ifstream ifs(USER_TOPIC_FILE);
            std::vector<std::string> lines;
            std::string line;
            std::string entryToRemove = cli.username + ":" + h.topic;

            while (std::getline(ifs, line))
                if (line != entryToRemove)
                    lines.push_back(line);
            ifs.close();

            std::ofstream ofs(USER_TOPIC_FILE, std::ios::trunc);
            for (auto &l : lines)
                ofs << l << "\n";
        }
        break;

    case MSG_PUBLISH_TEXT:
        // === LIST USERS ===
        if (topic_str == "/sys/get_users")
        {
            std::string list;
            for (auto &u : onlineUsers)
                list += u + "\n";

            PacketHeader ph{};
            ph.msgType = MSG_PUBLISH_TEXT;
            ph.payloadLength = (uint32_t)list.size();
            ph.timestamp = time(nullptr);
            ph.version = PROTOCOL_VERSION;
            strncpy(ph.topic, "/sys/user_list", MAX_TOPIC_LEN - 1);

            std::vector<uint8_t> pl(list.begin(), list.end());
            send_packet(c, ph, pl.data());
            send_ack(c, h.messageId);
            return;
        }

        // === GAME ===
        if (strncmp(h.topic, "/game/", 6) == 0)
        {
            handle_game(c, h, payload);
            send_ack(c, h.messageId);
            return;
        }

        // === PRIVATE / TOPIC ===
        if (h.flags & FLAG_PRIVATE)
        {
            if (!user_online(h.topic))
            {
                send_error(c, h.messageId, "User khong ton tai hoac offline!");
                return;
            }
            send_private(h.topic, h, payload);
        }
        else
        {
            if (cli.topics.count(h.topic) == 0)
            {
                send_error(c, h.messageId, "Ban chua subscribe topic nay!");
                return;
            }
            bool sent = false;
            for (auto &[c2, cli2] : g_clients)
            {
                if (cli2.topics.count(h.topic))
                {
                    send_packet(c2, h, payload);
                    sent = true;
                }
            }
            if (!sent)
                send_error(c, h.messageId, "Topic khong co subscriber!");
        }
        send_ack(c, h.messageId);
        break;

    case MSG_PUBLISH_FILE:
        // khởi tạo file
        {
            IncomingFile f;
            f.target = h.topic;
            f.sender = h.sender;
            f.is_private = h.flags & FLAG_PRIVATE;

            if (f.is_private && !user_online(f.target))
            {
                send_error(c, h.messageId, "User khong ton tai hoac offline!");
                return;
            }
            if (!f.is_private && !topic_has_subscribers(f.target))
            {
                send_error(c, h.messageId, "Topic khong co subscriber!");
                return;
            }

            std::string filename = (h.payloadLength > 0)
                                       ? std::string((char *)payload, h.payloadLength)
                                       : "upload_" + f.sender + "_" + f.target;
            f.filename = filename;
            f.ofs.open("upload/" + filename, std::ios::binary);
            if (!f.ofs)
            {
                send_error(c, h.messageId, "Cannot create file on server");
                return;
            }

            g_files[h.messageId] = std::move(f);
            send_ack(c, h.messageId);
        }
        break;

    case MSG_FILE_DATA:
    {
        auto it = g_files.find(h.messageId);
        if (it == g_files.end())
        {
            send_error(c, h.messageId, "File not found on server");
            return;
        }

        it->second.ofs.write((char *)payload, h.payloadLength);

        if (it->second.is_private)
            send_private(it->second.target, h, payload);
        else
            broadcast_topic(it->second.target, h, payload, c);

        // kết thúc file
        if (h.flags & FLAG_LAST)
        {
            it->second.ofs.close();
            std::cout << "File transfer completed: "
                      << it->second.sender << " -> " << it->second.target
                      << " (" << it->second.filename << ")\n";
            g_files.erase(it);
        }

        send_ack(c, h.messageId);
    }
    break;

    default:
        send_error(c, h.messageId, "INVALID_MSG");
    }
}

// ---------------- EVENT HANDLER ----------------
static void event_handler(mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_ACCEPT)
    {
        g_clients[c] = Client{};
    }
    else if (ev == MG_EV_HTTP_MSG)
    {
        auto *hm = (mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/websocket"), nullptr))
        {
            mg_ws_upgrade(c, hm, nullptr);
            g_clients[c].is_ws = true;
        }
    }
    else if (ev == MG_EV_WS_MSG)
    {
        auto *wm = (mg_ws_message *)ev_data;
        if (wm->data.len < sizeof(PacketHeader))
            return;

        PacketHeader h{};
        memcpy(&h, wm->data.buf, sizeof(h));

        std::vector<uint8_t> payload(h.payloadLength);
        if (h.payloadLength)
            memcpy(payload.data(), wm->data.buf + sizeof(h), h.payloadLength);

        handle_packet(c, h, payload.empty() ? nullptr : payload.data());
    }
    else if (ev == MG_EV_READ)
    {
        // TCP read
        while (c->recv.len >= sizeof(PacketHeader))
        {
            PacketHeader h{};
            memcpy(&h, c->recv.buf, sizeof(h));
            if (c->recv.len < sizeof(h) + h.payloadLength)
                break;

            std::vector<uint8_t> payload(h.payloadLength);
            if (h.payloadLength)
                memcpy(payload.data(), c->recv.buf + sizeof(h), h.payloadLength);

            handle_packet(c, h, payload.empty() ? nullptr : payload.data());
            mg_iobuf_del(&c->recv, 0, sizeof(h) + h.payloadLength);
        }
    }
    else if (ev == MG_EV_CLOSE)
    {
        if (!g_clients[c].username.empty())
        {
            std::string user = g_clients[c].username;

            // 1. Xóa user khỏi onlineUsers
            onlineUsers.erase(user);
            std::ofstream ofs(ONLINE_FILE, std::ios::trunc);
            for (auto &u : onlineUsers)
                ofs << u << "\n";

            // 2. Xóa tất cả entry của user trong user_topics.txt
            {
                std::ifstream ifs(USER_TOPIC_FILE);
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(ifs, line))
                {
                    if (line.find(user + ":") != 0)
                    { // nếu không phải user này
                        lines.push_back(line);
                    }
                }
                ifs.close();

                std::ofstream ofs(USER_TOPIC_FILE, std::ios::trunc);
                for (auto &l : lines)
                    ofs << l << "\n";
            }
        }

        // 3. Reset game nếu người chơi rời
        if (c == g_game.p1 || c == g_game.p2)
        {
            mg_connection *other = (c == g_game.p1) ? g_game.p2 : g_game.p1;
            if (other)
                send_game_text(other, "/game/abort", "opponent_left");
            g_game = GameRoom{};
            std::cout << "Game reset (player left)\n";
        }

        g_clients.erase(c);
    }
}

// ---------------- MAIN ----------------
int main()
{
    // reset các file
    std::ofstream(ONLINE_FILE, std::ios::trunc).close();
    std::ofstream(TOPICS_FILE, std::ios::trunc).close();
    std::ofstream(USER_TOPIC_FILE, std::ios::trunc).close();

    mg_mgr mgr;
    mg_mgr_init(&mgr);

    // lắng nghe WS & TCP
    mg_http_listen(&mgr, WS_PORT, event_handler, nullptr);
    mg_listen(&mgr, TCP_PORT, event_handler, nullptr);

    std::cout << "SERVER RUNNING\n";
    std::cout << "WS  : ws://localhost:8000/websocket\n";
    std::cout << "TCP : 8080\n";

    for (;;)
        mg_mgr_poll(&mgr, 500);

    mg_mgr_free(&mgr);
    return 0;
}
