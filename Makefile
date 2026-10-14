SDCC ?= sdcc
PYTHON ?= python3

BUILD := build
SOURCES := firmware/main.c firmware/usb.c firmware/dap.c firmware/swd.c firmware/c2.c firmware/isp.c firmware/uart.c firmware/jtag.c firmware/pic.c firmware/spi.c firmware/i2c.c
OBJECTS := $(patsubst firmware/%.c,$(BUILD)/%.rel,$(SOURCES))

.PHONY: all clean check load
all: $(BUILD)/fx2_swd.ihx

$(BUILD):
	mkdir -p $@

$(BUILD)/%.rel: firmware/%.c | $(BUILD)
	$(SDCC) -mmcs51 --std-c11 --model-large --opt-code-size -Ifirmware -c -o $@ $<

$(BUILD)/fx2_swd.ihx: $(OBJECTS)
	$(SDCC) -mmcs51 --model-large --code-size 0x3e00 --xram-size 0x0200 --xram-loc 0x3e00 -o $@ $(OBJECTS)
	$(PYTHON) tools/load_fx2.py --check $@

check: all
	$(PYTHON) -m unittest discover -s tests -v

load: all
	$(PYTHON) tools/load_fx2.py $(BUILD)/fx2_swd.ihx

clean:
	rm -rf $(BUILD)
