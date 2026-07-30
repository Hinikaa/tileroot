CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

CORE_SRCS = session.cpp matcher.cpp backend.cpp ipc_socket.cpp ipc_i3.cpp ipc_hyprland.cpp
CORE_HDRS = session.h matcher.h backend.h ipc_socket.h ipc_i3.h ipc_hyprland.h

tileroot: main.cpp $(CORE_SRCS) $(CORE_HDRS)
	$(CXX) $(CXXFLAGS) -o tileroot main.cpp $(CORE_SRCS)

test_session: test_session.cpp session.cpp session.h
	$(CXX) $(CXXFLAGS) -o test_session test_session.cpp session.cpp

test_matcher: test_matcher.cpp matcher.cpp matcher.h session.cpp session.h
	$(CXX) $(CXXFLAGS) -o test_matcher test_matcher.cpp matcher.cpp session.cpp

test: test_session test_matcher
	./test_session
	./test_matcher

clean:
	rm -f tileroot test_session test_matcher

.PHONY: test clean
