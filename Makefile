# ── Detect OS ────────────────────────────────────────────────
ifeq ($(OS),Windows_NT)
    PLATFORM := Windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := Darwin
    else
        PLATFORM := Linux
    endif
endif

# ── Platform-specific settings ───────────────────────────────
ifeq ($(PLATFORM),Darwin)
    CC      ?= clang
    SYM_PFX  = _
    SEC_DATA = .section __DATA,__wikipedia
    SEC_LEN  = .section __DATA,__wikilen
    EXE      =
    RM       = rm -f
endif

ifeq ($(PLATFORM),Linux)
    CC      ?= gcc
    SYM_PFX  =
    SEC_DATA = .section .rodata,"a",@progbits
    SEC_LEN  = .section .rodata,"a",@progbits
    EXE      =
    RM       = rm -f
endif

ifeq ($(PLATFORM),Windows)
    CC      ?= gcc
    SYM_PFX  = _
    SEC_DATA = .section .rdata,"dr"
    SEC_LEN  = .section .rdata,"dr"
    EXE      = .exe
    RM       = del /f /q
endif

CFLAGS  ?= -O2 -Wall -Wextra
LDFLAGS ?= -lcurl
PYTHON  ?= python3

TARGET  = pedantixsolver$(EXE)

# ── Targets ──────────────────────────────────────────────────
all: $(TARGET)

wikipedia_embed.s: wikipedia.bin
	@echo '.global $(SYM_PFX)wikipedia_bin'      >  $@
	@echo '.global $(SYM_PFX)wikipedia_bin_len'   >> $@
	@echo '$(SEC_DATA)'                            >> $@
	@echo '.balign 16'                             >> $@
	@echo '$(SYM_PFX)wikipedia_bin:'               >> $@
	@echo '  .incbin "wikipedia.bin"'              >> $@
	@echo '$(SYM_PFX)wikipedia_bin_end:'           >> $@
	@echo '$(SEC_LEN)'                             >> $@
	@echo '.balign 4'                              >> $@
	@echo '$(SYM_PFX)wikipedia_bin_len:'           >> $@
	@echo '  .long $(SYM_PFX)wikipedia_bin_end - $(SYM_PFX)wikipedia_bin' >> $@

wikipedia_bin.o: wikipedia_embed.s
	$(CC) -c -o $@ $<

$(TARGET): pedantixsolver.c wikipedia_bin.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) -a

extract:
	$(PYTHON) extract_wikipedia.py

format:
	$(PYTHON) format_wikipedia.py

clean:
ifeq ($(PLATFORM),Windows)
	$(RM) $(TARGET) *.o wikipedia_embed.s 2>nul || true
else
	$(RM) $(TARGET) *.o wikipedia_embed.s
endif

.PHONY: all clean run extract format
