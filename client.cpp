/*
 * ============================================================
 *  FILE   : client.cpp
 *  AUTHOR : Network Programming - W4
 *  DESC   : TCP Client sử dụng Winsock2 trên Windows.
 *           Kết nối tới Server, nhận danh sách số nguyên,
 *           tính tổng, gửi lại và nhận kết quả xác thực.
 *
 *  BIÊN DỊCH:
 *    [MinGW / g++]
 *      g++ -std=c++17 -o client.exe client.cpp -lws2_32
 *
 *    [MSVC / cl]
 *      cl /EHsc /std:c++17 client.cpp ws2_32.lib
 *
 *  CHẠY:
 *      client.exe                      -> mặc định 127.0.0.1:8080
 *      client.exe 192.168.1.10 8080    -> kết nối tới IP:Port chỉ định
 * ============================================================
 */

// ─── Bao gồm thư viện Windows Socket ───────────────────────
#define WIN32_LEAN_AND_MEAN          // Giảm bớt header không cần thiết của Windows
#include <winsock2.h>                // Winsock2 – thư viện TCP/IP chính
#include <ws2tcpip.h>                // inet_pton, getaddrinfo v.v.
#pragma comment(lib, "ws2_32.lib")   // Liên kết ws2_32 tự động (MSVC)

// ─── Thư viện chuẩn C++17 ──────────────────────────────────
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <stdexcept>   // std::stoi exception

// ─── Hằng số mặc định ──────────────────────────────────────
constexpr const char* DEFAULT_IP   = "127.0.0.1"; // IP server mặc định (localhost)
constexpr int         DEFAULT_PORT = 8080;         // Port mặc định

// ─── Hàm tiện ích: In lỗi Winsock kèm mã lỗi ──────────────
/**
 * @brief In thông báo lỗi ra stderr kèm mã WSAGetLastError().
 * @param context Chuỗi mô tả ngữ cảnh xảy ra lỗi.
 */
void printWsaError(const std::string& context)
{
    int errCode = WSAGetLastError();
    std::cerr << "[LOI] " << context
              << " | WSA Error Code: " << errCode << "\n";
}

// ─── Hàm tiện ích: Gửi toàn bộ chuỗi qua socket ───────────
/**
 * @brief Lặp lại send() cho đến khi gửi hết toàn bộ chuỗi str.
 *        Cần thiết vì TCP có thể tách nhỏ dữ liệu thành nhiều segment.
 * @param sock Socket đích.
 * @param str  Chuỗi cần gửi.
 * @return true nếu gửi thành công, false nếu có lỗi.
 */
bool sendAll(SOCKET sock, const std::string& str)
{
    const char* data  = str.c_str();
    int         total = static_cast<int>(str.size());
    int         sent  = 0;

    while (sent < total) {
        int result = send(sock, data + sent, total - sent, 0);
        if (result == SOCKET_ERROR) {
            printWsaError("send()");
            return false;
        }
        sent += result;
    }
    return true;
}

// ─── Hàm tiện ích: Nhận 1 dòng kết thúc bằng '\n' ─────────
/**
 * @brief Nhận từng byte từ sock đến khi gặp '\n'.
 *        Tự động loại bỏ '\r' để tránh lỗi parse trên Windows.
 * @param sock    Socket nguồn.
 * @param lineOut Chuỗi kết quả (không chứa '\n').
 * @return true nếu thành công, false nếu lỗi hoặc server đóng kết nối.
 */
bool recvLine(SOCKET sock, std::string& lineOut)
{
    lineOut.clear();
    char ch = '\0';

    while (true) {
        int result = recv(sock, &ch, 1, 0);

        if (result == 0) {
            // Server đã đóng kết nối
            std::cerr << "[INFO] Server da dong ket noi.\n";
            return false;
        }
        if (result == SOCKET_ERROR) {
            printWsaError("recv()");
            return false;
        }

        if (ch == '\n') break;          // Kết thúc dòng
        if (ch != '\r') lineOut += ch;  // Bỏ qua '\r' (Windows CRLF)
    }

    return true;
}

// ─── Hàm main ──────────────────────────────────────────────
int main(int argc, char* argv[])
{
    // ================================================================
    // BUOC 0: Đọc tham số dòng lệnh
    //         Cú pháp: client.exe [Server_IP] [Port]
    // ================================================================
    std::string serverIP   = DEFAULT_IP;
    int         serverPort = DEFAULT_PORT;

    if (argc >= 2) {
        serverIP = argv[1];  // Tham số thứ nhất: địa chỉ IP server
    }
    if (argc >= 3) {
        try {
            serverPort = std::stoi(argv[2]);  // Tham số thứ hai: port
            if (serverPort < 1 || serverPort > 65535) {
                std::cerr << "[LOI] Port phai nam trong khoang 1-65535.\n";
                return EXIT_FAILURE;
            }
        }
        catch (...) {
            std::cerr << "[LOI] Tham so port khong hop le: " << argv[2] << "\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "============================================\n";
    std::cout << "[CONFIG] Server IP   : " << serverIP   << "\n";
    std::cout << "[CONFIG] Server Port : " << serverPort << "\n";
    std::cout << "============================================\n";

    // ================================================================
    // BUOC 1: Khởi tạo Winsock 2.2
    // ================================================================
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        // Lỗi này xảy ra trước khi Winsock sẵn sàng nên không dùng WSAGetLastError()
        std::cerr << "[LOI] WSAStartup that bai. Ma loi: " << wsaResult << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Winsock khoi tao thanh cong (v"
              << LOBYTE(wsaData.wVersion) << "."
              << HIBYTE(wsaData.wVersion) << ").\n";

    // ================================================================
    // BUOC 2: Tạo TCP Socket
    //         AF_INET    = IPv4
    //         SOCK_STREAM = TCP (hướng kết nối, đảm bảo thứ tự byte)
    //         IPPROTO_TCP = Giao thức TCP
    // ================================================================
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printWsaError("socket()");
        WSACleanup();
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Socket duoc tao thanh cong.\n";

    // ================================================================
    // BUOC 3: Cấu hình địa chỉ server và kết nối (connect)
    // ================================================================
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;                                  // IPv4
    serverAddr.sin_port   = htons(static_cast<u_short>(serverPort));  // Chuyển sang big-endian

    // inet_pton chuyển chuỗi IP dạng text sang dạng binary 32-bit
    int inetResult = inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr);
    if (inetResult == 0) {
        std::cerr << "[LOI] Dia chi IP khong hop le: " << serverIP << "\n";
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }
    if (inetResult < 0) {
        printWsaError("inet_pton()");
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    // Thực hiện kết nối TCP (Three-way Handshake)
    std::cout << "[INFO] Dang ket noi toi " << serverIP << ":" << serverPort << " ...\n";
    if (connect(sock,
                reinterpret_cast<sockaddr*>(&serverAddr),
                sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printWsaError("connect()");
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Ket noi thanh cong!\n";
    std::cout << "--------------------------------------------\n";

    // ================================================================
    // BUOC 4: Nhận chuỗi "NUMBERS <N> <n1> ... <nN>\n" từ Server
    // ================================================================
    std::string rawMessage;
    if (!recvLine(sock, rawMessage)) {
        std::cerr << "[LOI] Khong the nhan du lieu tu server.\n";
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    // In chuỗi thô nhận được
    std::cout << "[RECV <- SERVER] " << rawMessage << "\n";

    // ================================================================
    // BUOC 5: Parse chuỗi nhận được
    //         Định dạng: "NUMBERS <N> <n1> <n2> ... <nN>"
    //         Dùng std::stringstream để đọc tuần tự từng token
    // ================================================================
    std::istringstream iss(rawMessage);
    std::string keyword;
    int N = 0;

    // Đọc từ khóa "NUMBERS"
    iss >> keyword;
    if (keyword != "NUMBERS") {
        std::cerr << "[LOI] Dinh dang tin khong hop le. Tu khoa nhan duoc: \""
                  << keyword << "\"\n";
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    // Đọc số lượng phần tử N
    if (!(iss >> N) || N <= 0) {
        std::cerr << "[LOI] Khong the doc so luong phan tu N.\n";
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    // Đọc N số nguyên vào vector
    std::vector<int> numbers(N);
    for (int i = 0; i < N; ++i) {
        if (!(iss >> numbers[i])) {
            std::cerr << "[LOI] Thieu du lieu: chi doc duoc " << i << "/" << N << " so.\n";
            closesocket(sock);
            WSACleanup();
            return EXIT_FAILURE;
        }
    }

    // In danh sách số đã tách
    std::cout << "[PARSE] So luong so (N) = " << N << "\n";
    std::cout << "[PARSE] Danh sach so   : ";
    for (int i = 0; i < N; ++i) {
        std::cout << numbers[i];
        if (i < N - 1) std::cout << ", ";
    }
    std::cout << "\n";

    // ================================================================
    // BUOC 6: Tính tổng S = n1 + n2 + ... + nN
    // ================================================================
    int sum = 0;
    for (int val : numbers) sum += val;

    std::cout << "[TINH TOAN] Tong S = ";
    for (int i = 0; i < N; ++i) {
        std::cout << numbers[i];
        if (i < N - 1) std::cout << " + ";
    }
    std::cout << " = " << sum << "\n";

    // ================================================================
    // BUOC 7: Gửi kết quả lên Server
    //         Định dạng: "RESULT <sum>\n"
    // ================================================================
    std::string response = "RESULT " + std::to_string(sum) + "\n";

    std::cout << "[SEND -> SERVER] " << response;  // response đã có '\n'

    if (!sendAll(sock, response)) {
        std::cerr << "[LOI] Khong the gui ket qua len server.\n";
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    // ================================================================
    // BUOC 8: Nhận phản hồi xác thực từ Server
    //         "STATUS CORRECT\n" hoặc "STATUS INCORRECT\n"
    // ================================================================
    std::string statusMsg;
    if (!recvLine(sock, statusMsg)) {
        std::cerr << "[LOI] Khong the nhan phan hoi xac thuc tu server.\n";
        closesocket(sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    std::cout << "[RECV <- SERVER] " << statusMsg << "\n";
    std::cout << "--------------------------------------------\n";

    // Hiển thị kết quả rõ ràng cho người dùng
    if (statusMsg == "STATUS CORRECT") {
        std::cout << "[THANH CONG] Server xac nhan: Tinh dung! Tong = " << sum << "\n";
    }
    else if (statusMsg == "STATUS INCORRECT") {
        std::cout << "[THAT BAI] Server xac nhan: Tinh sai!\n";
    }
    else {
        // Trường hợp server gửi phản hồi không xác định
        std::cout << "[CANH BAO] Phan hoi khong xac dinh: \"" << statusMsg << "\"\n";
    }

    // ================================================================
    // BUOC 9: Đóng kết nối và dọn dẹp tài nguyên
    // ================================================================
    closesocket(sock);   // Đóng socket, kết thúc phiên TCP
    WSACleanup();        // Giải phóng tài nguyên Winsock

    std::cout << "============================================\n";
    std::cout << "[INFO] Ket noi da dong. Chuong trinh ket thuc.\n";

    return EXIT_SUCCESS;
}
