# NVMe-lite DUT Makefile

CC = gcc
INJECT_BUG ?=
CFLAGS = -Wall -Wextra -std=c11 -O2 -g $(INJECT_BUG)
LDFLAGS = 

# Directories
SRC_DIR = src
VENDOR_DIR = vendor
BUILD_DIR = build
BIN_DIR = .

# Sources
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/seed.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/model.c \
       $(SRC_DIR)/scheduler.c \
       $(SRC_DIR)/logging.c \
       $(SRC_DIR)/runner.c \
       $(SRC_DIR)/rng.c \
       $(VENDOR_DIR)/mini_json.c

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))

TARGET = $(BIN_DIR)/nvme-lite-dut

.PHONY: all clean test

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(VENDOR_DIR) -c -o $@ $<

$(BUILD_DIR)/%.o: $(VENDOR_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Test targets
test: $(TARGET) test_determinism test_bound_k_zero test_fault_none

test_determinism: $(TARGET)
	@echo "=== Test 1: Determinism ==="
	@mkdir -p out/test
	@echo '{"seed_id":"test_det","commands":[{"type":"WRITE","lba":0,"len":4,"pattern":42},{"type":"READ","lba":0,"len":4},{"type":"FENCE"},{"type":"WRITE","lba":8,"len":2,"pattern":7}]}' > out/test/seed_det.json
	@./$(TARGET) run-one --seed-file out/test/seed_det.json --schedule-seed 42 --policy RANDOM --bound-k inf --fault-mode NONE --out-log out/test/run1.log --scheduler-version v1.0
	@./$(TARGET) run-one --seed-file out/test/seed_det.json --schedule-seed 42 --policy RANDOM --bound-k inf --fault-mode NONE --out-log out/test/run2.log --scheduler-version v1.0
	@if diff -q out/test/run1.log out/test/run2.log > /dev/null; then \
		echo "PASS: Logs are identical"; \
	else \
		echo "FAIL: Logs differ"; \
		diff out/test/run1.log out/test/run2.log; \
		exit 1; \
	fi

test_bound_k_zero: $(TARGET)
	@echo "=== Test 2: bound_k=0 forces FIFO order ==="
	@mkdir -p out/test
	@echo '{"seed_id":"test_bk0","commands":[{"type":"WRITE","lba":0,"len":1,"pattern":1},{"type":"WRITE","lba":1,"len":1,"pattern":2},{"type":"WRITE","lba":2,"len":1,"pattern":3},{"type":"WRITE","lba":3,"len":1,"pattern":4}]}' > out/test/seed_bk0.json
	@./$(TARGET) run-one --seed-file out/test/seed_bk0.json --schedule-seed 999 --policy ADVERSARIAL --bound-k 0 --fault-mode NONE --out-log out/test/bk0.log --scheduler-version v1.0
	@echo "Checking FIFO completion order (cmd_id=0,1,2,3)..."
	@grep "COMPLETE" out/test/bk0.log | head -4 > out/test/bk0_complete.txt
	@if grep -q "cmd_id=0" out/test/bk0_complete.txt && \
	    grep "COMPLETE" out/test/bk0.log | sed -n '1p' | grep -q "cmd_id=0" && \
	    grep "COMPLETE" out/test/bk0.log | sed -n '2p' | grep -q "cmd_id=1" && \
	    grep "COMPLETE" out/test/bk0.log | sed -n '3p' | grep -q "cmd_id=2" && \
	    grep "COMPLETE" out/test/bk0.log | sed -n '4p' | grep -q "cmd_id=3"; then \
		echo "PASS: FIFO order enforced with bound_k=0"; \
	else \
		echo "FAIL: Completion order not FIFO"; \
		cat out/test/bk0.log; \
		exit 1; \
	fi

test_fault_none: $(TARGET)
	@echo "=== Test 3: fault_mode=NONE -> pending_left=0 ==="
	@mkdir -p out/test
	@echo '{"seed_id":"test_fn","commands":[{"type":"WRITE","lba":0,"len":4,"pattern":42},{"type":"READ","lba":0,"len":4}]}' > out/test/seed_fn.json
	@./$(TARGET) run-one --seed-file out/test/seed_fn.json --schedule-seed 0 --policy FIFO --bound-k inf --fault-mode NONE --out-log out/test/fn.log --scheduler-version v1.0
	@if grep -q "RUN_END(pending_left=0" out/test/fn.log; then \
		echo "PASS: pending_left=0 for fault_mode=NONE"; \
	else \
		echo "FAIL: pending_left != 0"; \
		cat out/test/fn.log; \
		exit 1; \
	fi

test_reset: $(TARGET)
	@echo "=== Test 4: RESET fault mode ==="
	@mkdir -p out/test
	@echo '{"seed_id":"test_reset","commands":[{"type":"WRITE","lba":0,"len":1,"pattern":1},{"type":"WRITE","lba":1,"len":1,"pattern":2},{"type":"WRITE","lba":2,"len":1,"pattern":3},{"type":"WRITE","lba":3,"len":1,"pattern":4}]}' > out/test/seed_reset.json
	@./$(TARGET) run-one --seed-file out/test/seed_reset.json --schedule-seed 0 --policy FIFO --bound-k inf --fault-mode RESET --out-log out/test/reset.log --scheduler-version v1.0
	@if grep -q "RESET(reason=INJECTED" out/test/reset.log; then \
		echo "PASS: RESET event present"; \
	else \
		echo "FAIL: No RESET event"; \
		cat out/test/reset.log; \
		exit 1; \
	fi

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# Install (optional)
PREFIX ?= /usr/local
install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/
