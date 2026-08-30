CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread

# ถ้า compile บน Windows (MinGW) ต้อง link -lws2_32 (Winsock library) เพิ่ม
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
else
    LDFLAGS =
endif

all: buzz_server buzz_client

buzz_server: buzz_server.cpp netcompat.h
	$(CXX) $(CXXFLAGS) -o buzz_server buzz_server.cpp $(LDFLAGS)

buzz_client: buzz_client.cpp netcompat.h
	$(CXX) $(CXXFLAGS) -o buzz_client buzz_client.cpp $(LDFLAGS)

clean:
	rm -f buzz_server buzz_client buzz_server.exe buzz_client.exe
