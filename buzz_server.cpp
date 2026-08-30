#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include "netcompat.h"   // รวม Windows(Winsock)/Linux(POSIX)

#define PORT 9090
#define BUF_SIZE 2048
#define HOST_KEY "admin123"   // รหัสผ่านสำหรับ client ที่เป็น Host


struct Player {
    int id;
    std::string name;
    sockaddr_in addr;
    int score = 0;
};


std::map<int, Player> players;      // player_id -> Player
int nextPlayerId = 1;

std::vector<std::string> questions = {
    "How much is 3 plus 5?",
    "What is the capital of Japan?",
    "Which planet is closest to the Sun?",
    "How many minutes are there in 1 hour?",
    "What is the chemical formula for water?"
};
int currentQIndex = -1;  
bool questionActive = false; 
int buzzedPlayerId = -1;     
int serverSeq = 0;           

int sockFd;

std::vector<std::string> splitMessage(const std::string &msg, int maxTokens) {
    std::vector<std::string> tokens;
    std::istringstream iss(msg);
    std::string tok;
    while ((int)tokens.size() < maxTokens - 1 && (iss >> tok)) {
        tokens.push_back(tok);
    }
    std::string rest;
    std::getline(iss, rest);
    size_t start = rest.find_first_not_of(' ');
    if (start != std::string::npos) rest = rest.substr(start);
    else rest = "";
    if (!rest.empty()) tokens.push_back(rest);
    return tokens;
}

void sendTo(const std::string &msg, const sockaddr_in &addr) {
    sendto(sockFd, msg.c_str(), msg.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
}

void broadcast(const std::string &msg) {
    for (auto &p : players) {
        sendTo(msg, p.second.addr);
    }
    std::cout << "[SEND-ALL] " << msg << "\n";
}

void sendUnicast(const std::string &msg, const sockaddr_in &addr, const std::string &label) {
    sendTo(msg, addr);
    std::cout << "[SEND -> " << label << "] " << msg << "\n";
}

std::string findPlayerName(int id) {
    auto it = players.find(id);
    if (it == players.end()) return "UNKNOWN";
    return it->second.name;
}

// ---------- Command Handlers ----------
void handleRegister(const std::vector<std::string> &t, const sockaddr_in &addr) {
    // t = [BUZZ/1.0, seq, REGISTER, name]
    if (t.size() < 4) return;
    std::string name = t[3];
    int id = nextPlayerId++;

    Player p;
    p.id = id; p.name = name; p.addr = addr; p.score = 0;
    players[id] = p;

    std::ostringstream oss;
    oss << "BUZZ/1.0 " << t[1] << " 200 OK REGISTERED " << id << " " << name;
    sendUnicast(oss.str(), addr, name);
}

void handleBuzz(const std::vector<std::string> &t, const sockaddr_in &addr) {
    // t = [BUZZ/1.0, seq, BUZZ, player_id]
    if (t.size() < 4) return;
    int pid = std::stoi(t[3]);

    if (players.find(pid) == players.end()) {
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << t[1] << " 404 ERROR UNKNOWN_PLAYER";
        sendUnicast(oss.str(), addr, "?");
        return;
    }

    if (!questionActive) {
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << t[1] << " 430 LOCKED NO_ACTIVE_QUESTION";
        sendUnicast(oss.str(), addr, players[pid].name);
        return;
    }

    if (buzzedPlayerId != -1) {
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << t[1] << " 420 LOCKED ALREADY_BUZZED " << findPlayerName(buzzedPlayerId);
        sendUnicast(oss.str(), addr, players[pid].name);
        return;
    }

    // buzz ได้ก่อน lock buzzer แล้ว broadcast ทุกคน
    buzzedPlayerId = pid;
    serverSeq++;
    std::ostringstream oss;
    oss << "BUZZ/1.0 " << serverSeq << " 210 OK BUZZ_WINNER " << pid << " " << players[pid].name;
    broadcast(oss.str());
}

void handleAnswer(const std::vector<std::string> &t, const sockaddr_in &addr) {
    // t = [BUZZ/1.0, seq, ANSWER, player_id, text...]
    if (t.size() < 4) return;
    int pid = std::stoi(t[3]);
    std::string text = (t.size() >= 5) ? t[4] : "";

    if (pid != buzzedPlayerId) {
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << t[1] << " 400 ERROR NOT_YOUR_TURN";
        sendUnicast(oss.str(), addr, findPlayerName(pid));
        return;
    }
    serverSeq++;
    std::ostringstream oss;
    oss << "BUZZ/1.0 " << serverSeq << " 250 OK ANSWER_RECEIVED " << players[pid].name << " " << text;
    broadcast(oss.str());
}

bool checkHostKey(const std::string &key, const std::string &seq, const sockaddr_in &addr) {
    if (key != HOST_KEY) {
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << seq << " 401 ERROR UNAUTHORIZED";
        sendUnicast(oss.str(), addr, "HOST");
        return false;
    }
    return true;
}

void handleHostNext(const std::vector<std::string> &t, const sockaddr_in &addr) {
    // t = [BUZZ/1.0, seq, HOSTNEXT, hostkey]
    if (t.size() < 4) return;
    if (!checkHostKey(t[3], t[1], addr)) return;

    currentQIndex++;
    if (currentQIndex >= (int)questions.size()) {
        serverSeq++;
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << serverSeq << " 390 OK NO_MORE_QUESTIONS";
        broadcast(oss.str());
        currentQIndex--; // กันไม่ให้ index out of range
        return;
    }

    questionActive = true;
    buzzedPlayerId = -1;
    serverSeq++;
    std::ostringstream oss;
    oss << "BUZZ/1.0 " << serverSeq << " 201 OK NEW_QUESTION " << currentQIndex << " " << questions[currentQIndex];
    broadcast(oss.str());
}

void handleHostJudge(const std::vector<std::string> &t, const sockaddr_in &addr) {
    // t = [BUZZ/1.0, seq, HOSTJUDGE, hostkey, CORRECT|WRONG]
    if (t.size() < 5) return;
    if (!checkHostKey(t[3], t[1], addr)) return;

    if (buzzedPlayerId == -1) {
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << t[1] << " 400 ERROR NO_ONE_BUZZED";
        sendUnicast(oss.str(), addr, "HOST");
        return;
    }

    std::string verdict = t[4];
    int pid = buzzedPlayerId;

    if (verdict == "CORRECT") {
        players[pid].score += 10;
        questionActive = false;
        buzzedPlayerId = -1;
        serverSeq++;
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << serverSeq << " 300 OK RESULT_CORRECT " << players[pid].name
            << " SCORE " << players[pid].score;
        broadcast(oss.str());
    } else { // WRONG
        buzzedPlayerId = -1; // เปิด buzzer ใหม่ให้คนอื่นตอบต่อ
        serverSeq++;
        std::ostringstream oss;
        oss << "BUZZ/1.0 " << serverSeq << " 301 OK RESULT_WRONG " << players[pid].name;
        broadcast(oss.str());

        serverSeq++;
        std::ostringstream oss2;
        oss2 << "BUZZ/1.0 " << serverSeq << " 350 OK BUZZER_OPEN";
        broadcast(oss2.str());
    }
}

void handleHostReset(const std::vector<std::string> &t, const sockaddr_in &addr) {
    // t = [BUZZ/1.0, seq, HOSTRESET, hostkey]
    if (t.size() < 4) return;
    if (!checkHostKey(t[3], t[1], addr)) return;

    buzzedPlayerId = -1;
    serverSeq++;
    std::ostringstream oss;
    oss << "BUZZ/1.0 " << serverSeq << " 350 OK BUZZER_OPEN";
    broadcast(oss.str());
}

int main() {
    std::cout <<
    "   ____  _    _ ______ ______ \n"
    "  |  _ \\| |  | |___  /___  / \n"
    "  | |_) | |  | |  / /   / /  \n"
    "  |  _ <| |  | | / /   / /   \n"
    "  | |_) | |__| |/ /__ / /__  \n"
    "  |____/ \\____//_____/_____| \n"
    "  BUZZ/1.0  Quiz Buzzer Server\n"
    "  [+] Listening on UDP port " << PORT << "\n"
    "  [+] Host key: " << HOST_KEY << "\n\n";

    if (!netInit()) { std::cerr << "netInit failed\n"; return 1; }

    sockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd < 0) { perror("socket"); return 1; }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(sockFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind"); return 1;
    }

    char buf[BUF_SIZE];
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        ssize_t n = recvfrom(sockFd, buf, BUF_SIZE - 1, 0, (struct sockaddr*)&clientAddr, &clientLen);
        if (n <= 0) continue;
        buf[n] = '\0';
        std::string msg(buf);

        std::cout << "\n[RECV] " << msg << "\n";

        std::vector<std::string> peek = splitMessage(msg, 5);
        if (peek.size() < 3 || peek[0] != "BUZZ/1.0") {
            std::ostringstream oss;
            oss << "BUZZ/1.0 0 400 ERROR INVALID_FORMAT";
            sendUnicast(oss.str(), clientAddr, "?");
            continue;
        }

        std::string type = peek[2];
        if (type == "REGISTER") handleRegister(peek, clientAddr);
        else if (type == "BUZZ") handleBuzz(peek, clientAddr);
        else if (type == "ANSWER") handleAnswer(peek, clientAddr);
        else if (type == "HOSTNEXT") handleHostNext(peek, clientAddr);
        else if (type == "HOSTJUDGE") handleHostJudge(peek, clientAddr);
        else if (type == "HOSTRESET") handleHostReset(peek, clientAddr);
        else {
            std::ostringstream oss;
            oss << "BUZZ/1.0 " << peek[1] << " 400 ERROR UNKNOWN_COMMAND";
            sendUnicast(oss.str(), clientAddr, "?");
        }
    }

    CLOSESOCKET(sockFd);
    netCleanup();
    return 0;
}
