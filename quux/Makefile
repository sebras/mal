all:

step0_repl: step0_repl.o
step1_read_print: step1_read_print.o
step2_eval: step2_eval.o
step3_env: step3_env.o
step4_if_fn_do: step4_if_fn_do.o

BINS := step0_repl step1_read_print step2_eval step3_env step4_if_fn_do
LDLIBS := -lreadline

CFLAGS = -g -O0

all: $(BINS)

clean:
	@rm -f $(BINS) *.o
