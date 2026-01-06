#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>

#define DEFAULT_PORT 8080
#define MAX_BUFFER_SIZE 4096
#define MAX_TOPIC_LEN 32
#define MAX_USERNAME_LEN 32
#define PROTOCOL_VERSION 1

#define FLAG_PRIVATE 0x01
#define FLAG_GROUP   0x02
#define FLAG_FILE    0x04
#define FLAG_LAST    0x08

enum MessageType {
    MSG_LOGIN = 1,
    MSG_LOGOUT,
    MSG_SUBSCRIBE,
    MSG_UNSUBSCRIBE,
    MSG_PUBLISH_TEXT,
    MSG_PUBLISH_FILE,
    MSG_FILE_DATA,
    MSG_ERROR,
    MSG_ACK
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t msgType;
    uint32_t payloadLength;
    uint32_t messageId;
    uint64_t timestamp;
    uint8_t  version;
    uint8_t  flags;
    char sender[MAX_USERNAME_LEN];
    char topic[MAX_TOPIC_LEN];
    uint32_t checksum;
};
#pragma pack(pop)

#endif
