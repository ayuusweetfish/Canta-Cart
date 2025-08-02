CH573_EXAM_PATH ?= ~/Downloads/CH573EVT/EXAM
TOOLCHAIN_ROOT ?= ~/tools/xpack-riscv-none-elf-gcc-14.2.0-3
TOOL_PREFIX ?= $(TOOLCHAIN_ROOT)/bin/riscv-none-elf-
WCHISP ?= ~/tools/wchisp/target/release/wchisp
ISP55E0 ?= ~/Downloads/isp55e0/isp55e0

CC := $(TOOL_PREFIX)gcc
LD := $(TOOL_PREFIX)ld
OBJCOPY := $(TOOL_PREFIX)objcopy
OBJDUMP := $(TOOL_PREFIX)objdump
NM := $(TOOL_PREFIX)nm
RM ?= rm -f

# XXX: Currently unused due to excessive libc references
# When libc linkage is correctly configured, the use of `DEBUG()` is enabled
DEBUG_FLAGS =
ifeq ($(DEBUG), 1)
  DEBUG_FLAGS = -DDEBUG=Debug_UART1
endif

# xPack RISC-V GCC 14.2.0-3 does not include RV32IMAC, so downgrade to RV32IMC
# https://www.eevblog.com/forum/microcontrollers/warning-to-users-of-xpack-risc-v-gcc-distribution/?wap2

override CFLAGS := \
  -I $(CH573_EXAM_PATH)/SRC/StdPeriphDriver/inc \
  -I $(CH573_EXAM_PATH)/SRC/RVMSIS \
  -I $(CH573_EXAM_PATH)/BLE/LIB \
  -march=rv32imac_zifencei_zicsr -mabi=ilp32 -std=gnu11 -fdata-sections -ffunction-sections -O2 \
  $(DEBUG_FLAGS) \
  $(CFLAGS)

override LDFLAGS := \
  -T $(CH573_EXAM_PATH)/SRC/Ld/Link.ld --gc-sections $(LDFLAGS)
CRT_LIBS = \
  $(addprefix $(TOOLCHAIN_ROOT)/lib/gcc/riscv-none-elf/14.2.0/rv32imc/ilp32/, crtbegin.o crtend.o crti.o crtn.o libgcc.a) \
  $(TOOLCHAIN_ROOT)/riscv-none-elf/lib/rv32imc/ilp32/libc.a

SRCS_C = $(wildcard *.c)
LIB_SRCS_C = $(wildcard $(CH573_EXAM_PATH)/SRC/StdPeriphDriver/*.c)
LIB_SRCS_S = $(CH573_EXAM_PATH)/SRC/Startup/startup_CH573.S

OBJS = $(addsuffix .o, $(notdir $(basename $(wildcard $(LIB_SRCS_C))))) \
       $(addsuffix .o, $(notdir $(basename $(wildcard $(LIB_SRCS_S))))) \
       $(addsuffix .o, $(notdir $(basename $(wildcard $(SRCS_C)))))

all: main.elf main.bin

%.o: $(CH573_EXAM_PATH)/SRC/StdPeriphDriver/%.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: $(CH573_EXAM_PATH)/SRC/Startup/%.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

main.elf: $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS) \
	  $(CH573_EXAM_PATH)/SRC/StdPeriphDriver/libISP573.a \
	  $(CH573_EXAM_PATH)/BLE/LIB/libCH57xBLE.a \
	  $(CRT_LIBS)

main.bin: main.elf
	$(OBJCOPY) --input-target=ihex --output-target=binary main.elf main.bin

.PHONY: clean flash dump
clean:
	$(RM) $(OBJS) main.elf main.bin

flash: main.elf
	$(WCHISP) flash main.elf
	# $(ISP55E0) -f main.bin

dump: main.elf
	$(NM) --print-size --size-sort --radix=d main.elf
	$(OBJDUMP) -td main.elf
