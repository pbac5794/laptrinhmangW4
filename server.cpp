/*
 * ============================================================
 *  FILE   : server.cpp
 *  AUTHOR : Network Programming - W4
 *  DESC   : TCP Server sử dụng Winsock2 trên Windows.
 *           Server sinh ngẫu nhiên N số, gửi sang Client,
 *           nhận kết quả tổng từ Client, so sánh và phản hồi.
 *
 *  BIÊN DỊCH:
 *    [MinGW / g++]
 *      g++ -std=c++17 -o server.exe server.cpp -lws2_32
 *
 *    [MSVC / cl]
 *      cl /EHsc /std:c++17 server.cpp ws2_32.lib
 *
 *  CHẠY:
 *      server.exe          -> mặc định port 8080
 *      server.exe 9090     -> lắng nghe trên port 9090
 * ============================================================
 */

// ─── Bao gồm thư viện Windows Socket ───────────────────────
#define WIN32_LEAN_AND_MEAN          // Giảm bớt các header không cần thiết của Windows
#include <winsock2.h>                // Winsock2 – thư viện TCP/IP chính
#include <ws2tcpip.h>                // Hỗ trợ inet_ntop, getaddrinfo v.v.
#pragma comment(lib, "ws2_32.lib")   // Liên kết thư viện ws2_32 (dành cho MSVC)

// ─── Thư viện chuẩn C++17 ──────────────────────────────────
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <random>       // Sinh số ngẫu nhiên theo chuẩn C++11/17
#include <chrono>       // Lấy seed từ đồng hồ hệ thống
#include <cstring>      // memset, strstr
#include <stdexcept>    // std::stoi exception handling
#include <algorithm>    // std::remove

// ─── Hằng số mặc định ──────────────────────────────────────
constexpr int    DEFAULT_PORT    = 8080;   // Port mặc định nếu không truyền tham số
constexpr int    BACKLOG         = 10;     // Số lượng kết nối chờ tối đa trong hàng đợi
constexpr int    BUFFER_SIZE     = 1024;   // Kích thước buffer nhận / gửi dữ liệu
constexpr int    N_MIN           = 3;      // Số lượng số nguyên tối thiểu mỗi lần sinh
constexpr int    N_MAX           = 10;     // Số lượng số nguyên tối đa mỗi lần sinh
constexpr int    VAL_MIN         = 1;      // Giá trị nhỏ nhất của mỗi số nguyên
constexpr int    VAL_MAX         = 100;    // Giá trị lớn nhất của mỗi số nguyên

// ─── Hàm tiện ích: In lỗi Winsock kèm mã lỗi ──────────────
/**
 * @brief In thông báo lỗi ra stderr cùng mã lỗi WSAGetLastError().
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
 * @brief Gửi toàn bộ nội dung chuỗi str qua socket sock.
 *        Lặp lại send() cho đến khi tất cả byte được gửi đi.
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
 * @brief Nhận dữ liệu từ sock cho đến khi gặp ký tự '\n'.
 *        Xử lý loại bỏ '\r' (CRLF từ Windows/Telnet).
 * @param sock    Socket nguồn.
 * @param lineOut Chuỗi kết quả (không bao gồm '\n').
 * @return true nếu nhận thành công, false nếu có lỗi hoặc kết nối đóng.
 */
bool recvLine(SOCKET sock, std::string& lineOut)
{
    lineOut.clear();
    char ch = '\0';

    while (true) {
        int result = recv(sock, &ch, 1, 0);

        if (result == 0) {
            // Client đã đóng kết nối gracefully
            std::cout << "[INFO] Client da ngat ket noi.\n";
            return false;
        }
        if (result == SOCKET_ERROR) {
            printWsaError("recv()");
            return false;
        }

        if (ch == '\n') break;          // Kết thúc dòng
        if (ch != '\r') lineOut += ch;  // Bỏ qua '\r' (CRLF)
    }

    return true;
}

// ─── Hàm xử lý 1 phiên Client ──────────────────────────────
/**
 * @brief Xử lý toàn bộ giao tiếp với một client đã kết nối.
 *        Giao thức:
 *          Server -> Client: "NUMBERS <N> <n1> <n2> ... <nN>\n"
 *          Client -> Server: "RESULT <sum>\n"
 *          Server -> Client: "STATUS CORRECT\n" hoặc "STATUS INCORRECT\n"
 * @param clientSock Socket của client vừa accept().
 * @param clientAddr Thông tin địa chỉ của client.
 */
void handleClient(SOCKET clientSock, const sockaddr_in& clientAddr)
{
    // ── In thông tin địa chỉ IP và Port của client ──────────
    char clientIP[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET,                          // IPv4
              &clientAddr.sin_addr,             // Địa chỉ binary
              clientIP,                         // Buffer lưu chuỗi IP
              sizeof(clientIP));
    int clientPort = ntohs(clientAddr.sin_port);

    std::cout << "\n[KET NOI] Client ket noi tu "
              << clientIP << ":" << clientPort << "\n";

    // ── Thiết lập bộ sinh số ngẫu nhiên (chuẩn C++17) ───────
    // Dùng Mersenne Twister với seed từ đồng hồ hệ thống
    std::mt19937 rng(
        static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        )
    );
    std::uniform_int_distribution<int> distN(N_MIN, N_MAX);         // Số lượng số
    std::uniform_int_distribution<int> distVal(VAL_MIN, VAL_MAX);   // Giá trị mỗi số

    // ── Sinh N số ngẫu nhiên ────────────────────────────────
    int N = distN(rng);
    std::vector<int> numbers(N);
    int S_server = 0;

    for (int i = 0; i < N; ++i) {
        numbers[i] = distVal(rng);
        S_server += numbers[i];   // Tính tổng chính xác tại server
    }

    // ── Đóng gói chuỗi "NUMBERS <N> <n1> ... <nN>\n" ───────
    std::ostringstream oss;
    oss << "NUMBERS " << N;
    for (int val : numbers) oss << " " << val;
    oss << "\n";

    std::string message = oss.str();

    // In log gửi đi
    std::cout << "[SEND -> " << clientIP << ":" << clientPort << "] "
              << message;   // message đã có '\n' ở cuối

    // ── Gửi chuỗi sang client ───────────────────────────────
    if (!sendAll(clientSock, message)) {
        std::cerr << "[LOI] Khong the gui du lieu toi client.\n";
        closesocket(clientSock);
        return;
    }

    // ── Nhận phản hồi "RESULT <sum>\n" từ client ────────────
    std::string response;
    if (!recvLine(clientSock, response)) {
        // Lỗi hoặc client ngắt kết nối sớm
        closesocket(clientSock);
        return;
    }

    std::cout << "[RECV <- " << clientIP << ":" << clientPort << "] "
              << response << "\n";

    // ── Phân tích cú pháp "RESULT <sum>" ────────────────────
    // Kiểm tra tiền tố "RESULT "
    const std::string PREFIX = "RESULT ";
    if (response.rfind(PREFIX, 0) != 0) {
        // Định dạng không hợp lệ
        std::cerr << "[CANH BAO] Phan hoi khong dung dinh dang: \""
                  << response << "\"\n";
        sendAll(clientSock, "STATUS INCORRECT\n");
        closesocket(clientSock);
        return;
    }

    // Lấy phần số sau "RESULT "
    int S_client = 0;
    try {
        S_client = std::stoi(response.substr(PREFIX.size()));
    }
    catch (const std::exception& e) {
        std::cerr << "[CANH BAO] Khong the doc so tu phan hoi: "
                  << e.what() << "\n";
        sendAll(clientSock, "STATUS INCORRECT\n");
        closesocket(clientSock);
        return;
    }

    // ── So sánh kết quả và gửi phản hồi ─────────────────────
    std::cout << "[INFO] S_server=" << S_server
              << " | S_client=" << S_client << "\n";

    if (S_client == S_server) {
        std::cout << "[KET QUA] Client tinh DUNG.\n";
        sendAll(clientSock, "STATUS CORRECT\n");
    } else {
        std::cout << "[KET QUA] Client tinh SAI "
                  << "(Dung: " << S_server
                  << ", Nhan: " << S_client << ").\n";
        sendAll(clientSock, "STATUS INCORRECT\n");
    }

    // ── Đóng kết nối với client ──────────────────────────────
    closesocket(clientSock);
    std::cout << "[INFO] Da dong ket noi voi "
              << clientIP << ":" << clientPort << ".\n";
}

// ─── Hàm main ──────────────────────────────────────────────
int main(int argc, char* argv[])
{
    // ── Xác định Port từ tham số dòng lệnh ──────────────────
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        try {
            port = std::stoi(argv[1]);
            if (port < 1 || port > 65535) {
                std::cerr << "[LOI] Port phai nam trong khoang 1-65535.\n";
                return EXIT_FAILURE;
            }
        }
        catch (...) {
            std::cerr << "[LOI] Tham so port khong hop le: "
                      << argv[1] << "\n";
            return EXIT_FAILURE;
        }
    }

    // ================================================================
    // BUOC 1: Khởi tạo Winsock
    // ================================================================
    WSADATA wsaData;
    // MAKEWORD(2, 2) yêu cầu phiên bản Winsock 2.2
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        // WSAStartup thất bại – không thể dùng WSAGetLastError() ở đây
        std::cerr << "[LOI] WSAStartup that bai. Ma loi: "
                  << wsaResult << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Winsock khoi tao thanh cong. Phien ban: "
              << LOBYTE(wsaData.wVersion) << "."
              << HIBYTE(wsaData.wVersion) << "\n";

    // ================================================================
    // BUOC 2: Tạo socket lắng nghe (Listening Socket)
    // ================================================================
    // AF_INET    = IPv4
    // SOCK_STREAM = TCP (hướng kết nối, đảm bảo thứ tự byte)
    // IPPROTO_TCP = Giao thức TCP
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        printWsaError("socket()");
        WSACleanup();
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Socket lang nghe duoc tao thanh cong.\n";

    // ── Tuỳ chọn SO_REUSEADDR: cho phép tái sử dụng địa chỉ ─
    // Tránh lỗi "Address already in use" khi khởi động lại server nhanh
    int reuseOpt = 1;
    if (setsockopt(listenSock,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuseOpt),
                   sizeof(reuseOpt)) == SOCKET_ERROR)
    {
        printWsaError("setsockopt(SO_REUSEADDR)");
        // Không nghiêm trọng – vẫn tiếp tục
    }

    // ================================================================
    // BUOC 3: Gán địa chỉ (Bind)
    // ================================================================
    sockaddr_in serverAddr{};
    serverAddr.sin_family      = AF_INET;        // IPv4
    serverAddr.sin_addr.s_addr = INADDR_ANY;     // Lắng nghe trên tất cả NIC
    serverAddr.sin_port        = htons(static_cast<u_short>(port)); // Chuyển về big-endian

    if (bind(listenSock,
             reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printWsaError("bind()");
        closesocket(listenSock);
        WSACleanup();
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Bind thanh cong tren port " << port << ".\n";

    // ================================================================
    // BUOC 4: Chuyển sang chế độ lắng nghe (Listen)
    // ================================================================
    if (listen(listenSock, BACKLOG) == SOCKET_ERROR) {
        printWsaError("listen()");
        closesocket(listenSock);
        WSACleanup();
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Server dang lang nghe tren port " << port << "...\n";
    std::cout << "       Nhan Ctrl+C de dung server.\n";
    std::cout << "==========================================\n";

    // ================================================================
    // BUOC 5: Vòng lặp vô hạn – chấp nhận và xử lý từng Client
    // ================================================================
    while (true) {
        sockaddr_in clientAddr{};
        int         clientAddrLen = sizeof(clientAddr);

        std::cout << "\n[CHO] Dang doi ket noi moi...\n";

        // accept() – chặn (blocking) cho đến khi có client kết nối
        SOCKET clientSock = accept(listenSock,
                                   reinterpret_cast<sockaddr*>(&clientAddr),
                                   &clientAddrLen);

        if (clientSock == INVALID_SOCKET) {
            // Có thể xảy ra khi server bị ngắt bởi Ctrl+C hoặc lỗi mạng
            printWsaError("accept()");
            // Tiếp tục vòng lặp thay vì thoát hoàn toàn
            continue;
        }

        // ── Xử lý phiên làm việc với client ─────────────────
        // (Phiên bản đơn luồng – sequential, phục vụ lần lượt)
        handleClient(clientSock, clientAddr);
    }

    // ================================================================
    // BUOC 6: Dọn dẹp tài nguyên
    // (Thực tế khó đến được đây trong vòng lặp vô hạn,
    //  nhưng vẫn viết để đảm bảo tính đúng đắn của chương trình)
    // ================================================================
    closesocket(listenSock);
    WSACleanup();
    std::cout << "[INFO] Server da dung. Tai nguyen da giai phong.\n";

    return EXIT_SUCCESS;
}
