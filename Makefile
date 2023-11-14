# $(foreach var, list, text) : 여러 디렉터리에 특정 규칙을 적용할 떄 유용함.
# $(wildcard pattern) : 특정 패턴을 찾는 데 유용함.
# $(addprefix prefix, names) : 파일 경로를 조합할 때 유용하게 쓰이는 접두사 붙이기.

CC = gcc
CFLAGS = -Wall -g -W
LDFLAGS =

SRCDIRS = . system ui web_server

SRCS = $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.c))
OBJS = $(SRCS:.c=.o)

INCDIRS = $(foreach dir,$(SRCDIRS),-I $(dir))

TARGET_PATH = ./bin

TARGET = $(addprefix $(TARGET_PATH)/, toy_system)

all: $(TARGET)

$(TARGET): $(OBJS)
	mkdir ./bin 2> /dev/null || true
	$(CC) $(CFLAGS) $(INCDIRS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCDIRS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(TARGET) $(OBJS) $(TARGET_PATH)