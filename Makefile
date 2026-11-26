CXX ?= g++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wextra -MMD -MP
PREFIX ?= /usr/local

CORE_SRCS = session.cpp matcher.cpp backend.cpp ipc_socket.cpp ipc_i3.cpp ipc_hyprland.cpp x11_pid.cpp
CORE_OBJS = $(CORE_SRCS:.cpp=.o)

# libX11 is only needed by x11_pid.cpp (i3's GET_TREE has no "pid" field on
# window nodes, unlike sway's — see ipc_i3.cpp attach_cmdlines). Any system
# that can run i3 as its WM already has libX11 installed as a dependency of
# i3 itself, so this isn't a new user-facing dependency in practice.
tileroot: main.o $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o tileroot main.o $(CORE_OBJS) -lX11

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_session: test_session.o session.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o test_session test_session.o session.o

test_matcher: test_matcher.o matcher.o session.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o test_matcher test_matcher.o matcher.o session.o

test: test_session test_matcher
	./test_session
	./test_matcher

install: tileroot
	install -Dm755 tileroot $(DESTDIR)$(PREFIX)/bin/tileroot
	install -Dm644 completions/tileroot.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/tileroot
	install -Dm644 completions/tileroot.zsh $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_tileroot
	install -Dm644 completions/tileroot.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/tileroot.fish

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tileroot \
	      $(DESTDIR)$(PREFIX)/share/bash-completion/completions/tileroot \
	      $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_tileroot \
	      $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/tileroot.fish

clean:
	rm -f tileroot test_session test_matcher *.o *.d

-include $(wildcard *.d)

.PHONY: test install uninstall clean
