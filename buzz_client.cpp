#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include "netcompat.h"   // รวมความต่างของ Windows(Winsock)/Linux(POSIX) ไว้ที่นี่

#define SERVER_IP "127.0.0.1"
#define PORT 9090
#define BUF_SIZE 2048

int sockFd;
sockaddr_in serverAddr{};
std::atomic<bool> running(true);

int seq = 1;
int myPlayerId = -1;  // ได้จาก server ตอน REGISTER
std::string myName;
bool isHost = false;
std::string hostKey;

void sendMsg(const std::string &msg) {
    sendto(sockFd, msg.c_str(), msg.size(), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    std::cout << "[SEND] " << msg << "\n";
}

// แยกข้อความเป็น token
// index token: 0=BUZZ/1.0  1=seq  2=code  3=phrase1  4=phrase2  5+=payload
std::vector<std::string> splitAll(const std::string &msg) {
    std::vector<std::string> tokens;
    std::istringstream iss(msg);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

// เอา token ตั้งแต่ index startIdx ถึงตัวสุดท้ายมาต่อกันด้วยช่องว่าง
// ใช้สำหรับ field ที่เป็นข้อความยาว (เช่น เนื้อคำถาม, คำตอบ) ที่มีช่องว่างในตัวมันเอง
std::string joinFrom(const std::vector<std::string> &tokens, size_t startIdx) {
    std::ostringstream oss;
    for (size_t i = startIdx; i < tokens.size(); i++) {
        if (i > startIdx) oss << " ";
        oss << tokens[i];
    }
    return oss.str();
}

// อ่านสถานะ (status code) จากข้อความแล้วแปลเป็น respond ที่จะขึ้นบนจอ
void prettyPrint(const std::vector<std::string> &t, const std::string &raw) {
    std::cout << "[RECV] " << raw << "\n";
    if (t.size() < 3) return;
    std::string code = t[2];

    // BUZZ/1.0 seq 200 OK REGISTERED id name
    if (code == "200" && t.size() > 6) {
        myPlayerId = std::stoi(t[5]);
        std::cout << ">>> Registration successful! Your Player ID is " << myPlayerId << "\n";

    // BUZZ/1.0 seq 201 OK NEW_QUESTION qid text...
    } else if (code == "201" && t.size() > 6) {
        std::cout << ">>> [New Question #" << t[5] << "] " << joinFrom(t, 6) << "\n";

    // BUZZ/1.0 seq 210 OK BUZZ_WINNER id name
    } else if (code == "210" && t.size() > 6) {
        int pid = std::stoi(t[5]);
        std::string winner = t[6];
        if (isHost) std::cout << ">>> " << winner << " Buzz first! Wait for the answer, then type correct/wrong\n";
        else if (pid == myPlayerId) std::cout << ">>> You buzzed first! You can enter your answer now.\n";
        else std::cout << ">>> " << winner << " buzzed before you...\n";

    // BUZZ/1.0 seq 420 LOCKED ALREADY_BUZZED name
    } else if (code == "420" && t.size() > 5) {
        std::cout << ">>> You didn't buzz in fast enough! " << joinFrom(t, 5) << " buzzed first\n";

    // BUZZ/1.0 seq 430 LOCKED NO_ACTIVE_QUESTION
    } else if (code == "430") {
        std::cout << ">>> There is no question right now.\n";

    // BUZZ/1.0 seq 250 OK ANSWER_RECEIVED name text...
    } else if (code == "250" && t.size() > 5) {
        std::cout << ">>> Answer from " << t[5] << ": " << joinFrom(t, 6) << "\n";

    // BUZZ/1.0 seq 300 OK RESULT_CORRECT name SCORE score
    } else if (code == "300" && t.size() > 7) {
        std::cout << ">>> correct! " << t[5] << " Total score " << t[7] << " Points\n";

    // BUZZ/1.0 seq 301 OK RESULT_WRONG name
    } else if (code == "301" && t.size() > 5) {
        std::cout << ">>> wrong! " << t[5] << " answered incorrectly.\n";

    } else if (code == "350") {
        std::cout << ">>> The buzzer is ready.\n";
    } else if (code == "390") {
        std::cout << ">>> No more questions. Game over!\n";
    } else if (code == "400" || code == "401" || code == "404") {
        std::cout << ">>> [ERROR] " << raw << "\n";
    }
}

void receiveLoop() {
    char buf[BUF_SIZE];
    while (running) {
        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);
        ssize_t n = recvfrom(sockFd, buf, BUF_SIZE - 1, 0, (struct sockaddr*)&fromAddr, &fromLen);
        if (n <= 0) continue;
        buf[n] = '\0';
        std::string msg(buf);
        std::vector<std::string> t = splitAll(msg);
        prettyPrint(t, msg);
    }
}

void playerLoop() {
    // 1) ลงทะเบียนก่อน
    std::ostringstream reg;
    reg << "BUZZ/1.0 " << seq++ << " REGISTER " << myName;
    sendMsg(reg.str());

    std::cout << "\nType 'buzz' and press Enter to buzz in, or type your answer and press Enter to submit it. (Type 'quit' to exit.)\n";

    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (line == "quit") { running = false; break; }
        if (myPlayerId == -1) {
            std::cout << "!! Registration has not been completed yet. Please wait a moment.\n";
            continue;
        }
        std::ostringstream oss;
        if (line == "buzz") {
            oss << "BUZZ/1.0 " << seq++ << " BUZZ " << myPlayerId;
        } else {
            oss << "BUZZ/1.0 " << seq++ << " ANSWER " << myPlayerId << " " << line;
        }
        sendMsg(oss.str());
    }
}

void hostLoop() {
    std::cout << "\n=== HOST CONTROL ===\n"
                 "Available commands: next | correct | wrong | reset | quit\n";
    std::string line;
    while (running && std::getline(std::cin, line)) {
        std::ostringstream oss;
        if (line == "next") {
            oss << "BUZZ/1.0 " << seq++ << " HOSTNEXT " << hostKey;
        } else if (line == "correct") {
            oss << "BUZZ/1.0 " << seq++ << " HOSTJUDGE " << hostKey << " CORRECT";
        } else if (line == "wrong") {
            oss << "BUZZ/1.0 " << seq++ << " HOSTJUDGE " << hostKey << " WRONG";
        } else if (line == "reset") {
            oss << "BUZZ/1.0 " << seq++ << " HOSTRESET " << hostKey;
        } else if (line == "quit") {
            running = false; break;
        } else {
            std::cout << "!! Invalid command: next | correct | wrong | reset | quit\n";
            continue;
        }
        sendMsg(oss.str());
    }
}

int main(int argc, char *argv[]) {
    // --- manual argument parsing (ไม่ใช้ library ภายนอก) ---
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--name" && i + 1 < argc) { myName = argv[++i]; }
        else if (a == "--host" && i + 1 < argc) { isHost = true; hostKey = argv[++i]; }
    }

    if (!isHost && myName.empty()) {
        std::cerr << "Usage:\n"
                     "  Player mode : " << argv[0] << " --name <YourName>\n"
                     "  Host mode   : " << argv[0] << " --host <HostKey>\n";
        return 1;
    }

    if (!netInit()) { std::cerr << "netInit failed\n"; return 1; }

    sockFd = socket(AF_INET, SOCK_DGRAM, 0);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    std::cout << "===========================================\n"
                 " BUZZ/1.0 Client  |  mode: " << (isHost ? "HOST" : "PLAYER") << "\n"
                 "===========================================\n";

    std::thread t(receiveLoop);
    t.detach();

    if (isHost) hostLoop();
    else playerLoop();

    running = false;
    CLOSESOCKET(sockFd);
    netCleanup();
    return 0;
}