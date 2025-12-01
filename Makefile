# 检测操作系统
ifeq ($(OS),Windows_NT)
    detected_OS := Windows
else
    detected_OS := $(shell uname -s)
endif

# 根据操作系统设置可执行文件扩展名
ifeq ($(detected_OS),Windows)
    TARGET_EXTENSION = .exe
    MKDIR = if not exist $(subst /,\,$(1)) mkdir $(subst /,\,$(1))
    RMDIR = if exist $(subst /,\,$(1)) rmdir /s /q $(subst /,\,$(1))
    RM = if exist $(subst /,\,$(1)) del /q $(subst /,\,$(1))
else
    TARGET_EXTENSION =
    MKDIR = mkdir -p $(1)
    RMDIR = rm -rf $(1)
    RM = rm -f $(1)
endif

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

# Windows 和 Unix 的文件查找方式不同
ifeq ($(detected_OS),Windows)
    SOURCES = $(shell dir /s /b *.c 2>nul)
else
    SOURCES = $(shell find $(SRCDIR) -name '*.c')
endif

OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

TARGET = $(BINDIR)/dbipatcher$(TARGET_EXTENSION)

# 默认目标
all: $(TARGET)

$(TARGET): $(OBJECTS)
	@$(call MKDIR,$(BINDIR))
	$(CC) $(OBJECTS) -o $@ $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@$(call MKDIR,$(dir $@))
	$(CC) $(CFLAGS) -c $< -o $@

# 清理构建文件
clean:
	@$(call RMDIR,$(BUILDDIR))
	@$(call RM,$(TARGET))

# 运行测试
run: $(TARGET)
	@$(TARGET)

# 显示构建信息
info:
	@echo "Platform: $(detected_OS)"
	@echo "Target: $(TARGET)"
	@echo "Sources: $(words $(SOURCES)) files"

.PHONY: all clean run info
