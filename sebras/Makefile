SRCS = step0_repl.c step1_read_print.c
OBJS = $(SRCS:%.c=%.o)
BINS = $(OBJS:%.o=%)

CFLAGS = -O0 -g -fno-omit-frame-pointer
LDFLAGS = -lreadline

all: $(BINS)

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $(@:%.o=%.c) -o $@

$(BINS): %: %.o
	$(CC) $+ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINS)
