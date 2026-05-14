ifeq ($(origin CC),default)
	CC = gcc
endif
CLANG_STATIC ?= clang
AR ?= ar

CFLAGS = \
	-std=c11 \
	-O2

ANALYZE_CFLAGS = \
	-std=c11

LDFLAGS = -pthread -lm -pie

ifeq ($(SAN),address)
	CFLAGS += -g -O1 -fno-omit-frame-pointer -fsanitize=address
	LDFLAGS += -fsanitize=address
else ifeq ($(SAN),undefined)
	CFLAGS += -g -O1 -fno-omit-frame-pointer -fsanitize=undefined
	LDFLAGS += -fsanitize=undefined
else ifeq ($(SAN),memory)
	CC = clang
	CFLAGS += -g -O1 -fno-omit-frame-pointer -fsanitize=memory -fsanitize-memory-track-origins
	LDFLAGS += -fsanitize=memory -fsanitize-memory-track-origins
else ifneq ($(SAN),)
	$(error Unknown SAN='$(SAN)'. Use SAN=address, SAN=undefined or SAN=memory)
endif

SECURITY_COMMON_CFLAGS = \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Werror \
	-fwrapv \
	-fno-strict-aliasing \
	-fno-delete-null-pointer-checks \
	-D_FORTIFY_SOURCE=2 \
	-fstack-protector-strong \
	-fPIE \
	-fPIC \
	-Warray-bounds \
	-Wdiv-by-zero \
	-Wshift-count-negative \
	-Wshift-count-overflow

SECURITY_GCC_CFLAGS = \
	-fwrapv-pointer \
	-Wclobbered

ifneq ($(findstring clang,$(notdir $(CC))),)
	SECURITY_CFLAGS = $(SECURITY_COMMON_CFLAGS)
else
	SECURITY_CFLAGS = $(SECURITY_COMMON_CFLAGS) $(SECURITY_GCC_CFLAGS)
endif

CFLAGS += $(SECURITY_CFLAGS)
ANALYZE_CFLAGS += $(SECURITY_COMMON_CFLAGS)

BUILD_DIR = build
COMMON_OBJ = $(BUILD_DIR)/common.o
CLIENT_OBJ = $(BUILD_DIR)/client.o
SERVER_OBJ = $(BUILD_DIR)/server.o
LIB = $(BUILD_DIR)/libdistributed_integral.a
CONNECT_TIMEOUT_MS ?= 120000
COMPUTE_TIMEOUT_MS ?= 120000
SERVER_TIMEOUT_MS ?= 120000
FAIL_TIMEOUT_MS ?= 300

TEST_ENV = \
	CONNECT_TIMEOUT_MS=$(CONNECT_TIMEOUT_MS) \
	COMPUTE_TIMEOUT_MS=$(COMPUTE_TIMEOUT_MS) \
	SERVER_TIMEOUT_MS=$(SERVER_TIMEOUT_MS) \
	FAIL_TIMEOUT_MS=$(FAIL_TIMEOUT_MS)

PROGRAMS = \
	$(BUILD_DIR)/client \
	$(BUILD_DIR)/server \
	$(BUILD_DIR)/integral \
	$(BUILD_DIR)/integral2 \
	$(BUILD_DIR)/library_workers \
	$(BUILD_DIR)/library_server

default: $(PROGRAMS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/common.o: common.c common.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c common.c -o $@

$(BUILD_DIR)/client.o: client.c common.h distributed.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c client.c -o $@

$(BUILD_DIR)/server.o: server.c common.h distributed.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c server.c -o $@

$(LIB): $(COMMON_OBJ) $(CLIENT_OBJ) $(SERVER_OBJ) | $(BUILD_DIR)
	$(AR) rcs $@ $(COMMON_OBJ) $(CLIENT_OBJ) $(SERVER_OBJ)

$(BUILD_DIR)/client: client_main.c distributed.h $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) client_main.c $(LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/server: server_main.c distributed.h $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) server_main.c $(LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/integral: integral.c common.h distributed.h $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) integral.c $(LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/integral2: integral2.c common.h distributed.h $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) integral2.c $(LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/library_workers: tests/library_workers.c common.h distributed.h $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. tests/library_workers.c $(LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/library_server: tests/library_server.c common.h distributed.h $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. tests/library_server.c $(LIB) -o $@ $(LDFLAGS)

run: default
	./$(BUILD_DIR)/integral 0 0 3.141592653589793 30000000 3 1338 1 1 1
	./$(BUILD_DIR)/integral 0 0 3.141592653589793 30000000 3 1339 2 2 2

test: default
	$(TEST_ENV) ./run_integral2_test.sh 0 0 3.141592653589793 1000000 1338 1 1 1
	$(TEST_ENV) ./run_integral2_test.sh 0 0 3.141592653589793 1000000 1338 2 2 2
	$(TEST_ENV) ./run_integral2_test.sh 0 0 3.141592653589793 1000000 1338 4 4 4
	$(TEST_ENV) ./run_integral2_test.sh 1 0 1.5707963267948966 1000000 1339 1 1 1
	$(TEST_ENV) ./run_integral2_test.sh 2 0 1 1000000 1340 1 1 1
	$(TEST_ENV) ./run_failure_test.sh worker 1341 1000000 1
	$(TEST_ENV) ./run_failure_test.sh server 1342 1000000 1
	$(TEST_ENV) ./tests/run_library_failure_test.sh worker 1343 100000 1
	$(TEST_ENV) ./tests/run_library_failure_test.sh server 1344 100000 1
	$(TEST_ENV) ./tests/run_timeout_test.sh client 1345
	$(TEST_ENV) ./tests/run_timeout_test.sh server 1346

docs:
	doxygen Doxyfile

clean-docs:
	rm -rf docs/html

clean:
	rm -rf $(BUILD_DIR)

asan:
	$(MAKE) clean
	$(MAKE) SAN=address

ubsan:
	$(MAKE) clean
	$(MAKE) SAN=undefined

msan:
	$(MAKE) clean
	$(MAKE) SAN=memory

clang:
	$(MAKE) clean
	$(MAKE) CC=clang

sanitize-test:
	$(MAKE) clean
	$(MAKE) SAN=address
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) SAN=undefined
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) SAN=memory
	$(MAKE) test

analyze:
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) common.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) client.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) server.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) integral.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) integral2.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) client_main.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) server_main.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) -I. tests/library_workers.c
	$(CLANG_STATIC) --analyze $(ANALYZE_CFLAGS) -I. tests/library_server.c

.PHONY: default run test docs clean-docs clean asan ubsan msan clang sanitize-test analyze
