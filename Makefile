# 检测操作系统
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# 根据操作系统设置可执行文件扩展名
ifeq ($(UNAME_S),Windows_NT)
    TARGET_EXTENSION = .exe
else
    TARGET_EXTENSION =
endif

# 根据架构和操作系统设置平台
ifeq ($(UNAME_S),Linux)
    PLATFORM = linux
else ifeq ($(UNAME_S),Darwin)
    PLATFORM = macos
else
    PLATFORM = unknown
endif

ARCH = $(UNAME_M)

CC = gcc

CFLAGS = -std=gnu11 \
	-g \
	-I$(SRCDIR)/inc \
	-Wall \
	-Wextra \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-Wno-sign-compare \
	-Wno-unused-function

# 如果需要 zstd 库，取消注释
# LDLIBS = -lzstd

SRCDIR = src
BUILDDIR = build
BINDIR = bin

SOURCES = $(shell find $(SRCDIR) -name '*.c')

OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

TARGET = $(BINDIR)/dbipatcher$(TARGET_EXTENSION)

# 默认目标
all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILDDIR) $(BINDIR)
	$(CC) $(OBJECTS) -o $@ $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

# 清理构建文件
clean:
	@rm -rf $(BUILDDIR)
	@rm -f $(TARGET)

# 运行测试
run: $(TARGET)
	@$(TARGET)

# 使用 Valgrind 调试 (仅 Linux)
debug: $(TARGET)
ifeq ($(UNAME_S),Linux)
	@valgrind $(TARGET)
else
	@echo "Valgrind is only available on Linux"
	@$(TARGET)
endif

# 显示构建信息
info:
	@echo "Platform: $(PLATFORM)"
	@echo "Architecture: $(ARCH)"
	@echo "Target: $(TARGET)"

# 安装依赖 (根据不同平台)
install-deps:
ifeq ($(UNAME_S),Linux)
	sudo apt-get update
	sudo apt-get install -y build-essential
	# 如果需要: sudo apt-get install -y libzstd-dev
else ifeq ($(UNAME_S),Darwin)
	brew update
	# 如果需要: brew install zstd
endif

.PHONY: all clean run debug info install-deps
