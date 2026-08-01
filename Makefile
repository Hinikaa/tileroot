CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

CORE_SRCS = session.cpp matcher.cpp backend.cpp ipc_socket.cpp ipc_i3.cpp ipc_hyprland.cpp x11_pid.cpp
CORE_HDRS = session.h matcher.h backend.h ipc_socket.h ipc_i3.h ipc_hyprland.h x11_pid.h

# libX11 is only needed by x11_pid.cpp (i3's GET_TREE has no "pid" field on
# window nodes, unlike sway's — see ipc_i3.cpp attach_cmdlines). Any system
# that can run i3 as its WM already has libX11 installed as a dependency of
# i3 itself, so this isn't a new user-facing dependency in practice.
tileroot: main.cpp $(CORE_SRCS) $(CORE_HDRS)
	$(CXX) $(CXXFLAGS) -o tileroot main.cpp $(CORE_SRCS) -lX11

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
