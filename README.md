# Chat Publish/Subscribe – INT3304

## Môn học: **INT3304 – Lập trình mạng**
# Nhóm thực hiện 
Nguyễn Đức Hoan 23020607

Ngô Đức Huy 23020610

Võ Hồng Thái 23020644

# Tài liệu
https://www.overleaf.com/read/zjxnddrtvjnv#6d8b76
## 1. Giới thiệu

Đây là đồ án **Bài Tập Lớn môn Lập trình mạng (INT3304)**, xây dựng hệ thống **chat theo mô hình Publish/Subscribe**.

* **Server**: đóng vai trò **Broker**, quản lý kết nối, phân phối thông điệp.
* **Client**: vừa là **Publisher** (gửi dữ liệu) vừa là **Subscriber** (nhận dữ liệu).

Chương trình hỗ trợ:

* Chat 1–1
* Chat nhóm
* Gửi file cá nhân và theo nhóm
* Chức năng mở rộng: chơi game (Tic-Tac-Toe)

---

## 2. Môi trường & công nghệ

* Hệ điều hành: **Windows** (có thể mở rộng Linux)
* Ngôn ngữ: **C++**
* Giao thức: **TCP Socket + WebSocket (mongoose)**
* Thư viện:

  * `winsock2`
  * `ws2tcpip`
  * `mongoose.c`

---

## 3. Cấu trúc thư mục

```
.
├── client.cpp        # Chương trình client
├── server.cpp        # Chương trình server (broker)
├── protocol.h        # Định nghĩa giao thức
├── mongoose.c        # WebSocket library
├── README.md
```

---

## 4. Cấu hình trước khi chạy (Setup)

Sử dụng Window hoặc Linux
Có sử dụng VSCode

---

## 5. Build chương trình

Mở **Command Prompt / PowerShell** tại thư mục source code.

### 5.1. Build client

```cmd
g++ client.cpp -o client.exe -lws2_32 -lcomdlg32 -pthread
```

### 5.2. Build server

```cmd
g++ server.cpp mongoose.c -o server.exe -lws2_32 -pthread
```

Sau khi build thành công sẽ thu được:

* `server.exe`
* `client.exe`

---

## 6. Chạy chương trình

### 6.1. Khởi động server

Chạy **server trước**:

```cmd
.\server.exe
```

### 6.2. Khởi động client

Mở **2 cửa sổ terminal khác nhau**, mỗi cửa sổ chạy:

```cmd
.\client.exe
```

---

## 7. Kịch bản kiểm thử (Test Scenarios)

### Kịch bản 1: Chat cá nhân

1. Client A đăng nhập
2. Client B đăng nhập
3. Client A gửi tin nhắn riêng cho B
4. Client B nhận tin nhắn ngay lập tức

---

### Kịch bản 2: Chat nhóm

1. Client A tạo topic (group)
2. Client B subscribe vào topic
3. Client A gửi tin nhắn vào group
4. Tất cả client trong group nhận được tin nhắn

---

### Kịch bản 3: Gửi file cá nhân

1. Client A chọn gửi file cho Client B
2. File được chia nhỏ, gửi qua TCP
3. Client B nhận file và lưu vào thư mục `upload/`

---

### Kịch bản 4: Gửi file theo nhóm

1. Client A gửi file vào topic
2. Các client đã subscribe topic nhận được file

---

### Kịch bản 5: Chơi game (Tic-Tac-Toe)

1. Client A gửi lời mời chơi game
2. Client B chấp nhận
3. Hai client gửi/nhận nước đi thông qua server
4. Server đóng vai trò relay, không can thiệp logic

---

## 8. Ghi chú

* Server chỉ đóng vai trò **Broker**, không xử lý nội dung logic ứng dụng
* Giao thức truyền tin được định nghĩa trong `protocol.h`
* Client vừa publish vừa subscribe theo đúng mô hình Pub/Sub

