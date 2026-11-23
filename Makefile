CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
RUSTC = rustc
PKG_CONFIG = pkg-config

C_PROGRAMS = file_demo recursive_ls typing process_signal fifo_demo pipe_chat mq_chat shm_copy
PROGRAMS = $(C_PROGRAMS) matrix_add 

.PHONY: all clean
all: $(addprefix bin/,$(PROGRAMS))

bin:
	mkdir -p $@

bin/%: src/%.c | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

bin/matrix_add: src/matrix_add.rs | bin
	env -u MAKEFLAGS -u MFLAGS $(RUSTC) --edition=2021 $< -o $@

bin/recursive_ls: src/recursive_ls.c src/list_dir.c src/list_dir.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) $(LDLIBS) -o $@

bin/mini_shell: src/mini_shell.c src/shell_commands.c src/shell_commands.h src/list_dir.c src/list_dir.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) $(LDLIBS) -o $@

bin/file_demo bin/fifo_demo bin/pipe_chat bin/shm_copy bin/mini_shell: src/io.h
bin/pipe_chat bin/mq_chat: src/chat.h
bin/mq_chat bin/shm_copy: LDLIBS += -lrt

bin/notify_server bin/notify_client: bin/notify_%: src/notify_%.c src/notify.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $$($(PKG_CONFIG) --cflags gio-2.0) $< $(LDFLAGS) $$($(PKG_CONFIG) --libs gio-2.0) $(LDLIBS) -o $@

clean:
	rm -rf bin
