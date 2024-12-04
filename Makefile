#     ____                    _        ____
#    |  _ \  ___  _ __   __ _| | ___  |  _ \ _   _ _ __ ___  _ __   ___ _ __
#    | | | |/ _ \| '_ \ / _` | |/ _ \ | | | | | | | '_ ` _ \| '_ \ / _ \ '__|
#    | |_| | (_) | | | | (_| | |  __/ | |_| | |_| | | | | | | |_) |  __/ |
#    |____/ \___/|_| |_|\__, |_|\___| |____/ \__,_|_| |_| |_| .__/ \___|_|
#                       |___/                               |_|
# security dongle dumper for PlayStation2 based namco system 246/256
.SILENT:
MGKEY ?= ARCADE
EE_BIN = KELF_BINDER_$(MGKEY).ELF

EE_OBJS = $(addprefix src/,$(addsuffix .o, main downloadfile ioprp usbd bdm bdmfs_fatfs usbmass_bd fileXio iomanX secrsif_mechaemu mcman mcserv padman sio2man))

TTY = PPC
ifeq ($(TTY), PPC)
EE_OBJS += src/ppctty.o
EE_CFLAGS += -DTTY=1
else ifeq ($(TTY), UDP)
EE_OBJS += src/ps2dev9.o src/udptty_standalone.o
EE_CFLAGS += -DTTY=2
endif

EE_CFLAGS += -fdata-sections -ffunction-sections -DNEWLIB_PORT_AWARE -DMGKEY=\"$(MGKEY)\"
EE_LDFLAGS += -Wl,--gc-sections
EE_LIBS += $(addprefix -l, iopreboot debug patches fileXio cdvd mc padx)

ifeq ($(DEBUG), 1)
  EE_CFLAGS += -DDEBUG -O0 -g
else
  EE_CFLAGS += -Os
  EE_LDFLAGS += -s
endif

all: $(EE_BIN)
	$(info $(EE_BIN): built)

clean:
	rm -rf $(EE_OBJS) $(EE_BIN) src/ioprp.c

.INTERMEDIATE: src/ioprp.c
src/ioprp.c: IOPRP_$(MGKEY).IMG
	bin2c $< $@ ioprp


vpath %.IMG iop/
vpath %.irx iop/
vpath %.irx $(PS2SDK)/iop/irx/
IRXTAG = $(notdir $(addsuffix _irx, $(basename $<)))
src/%.c: %.irx
	$(DIR_GUARD)
	@bin2c $< $@ $(IRXTAG)

# Include makefiles
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal