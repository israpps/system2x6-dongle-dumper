#     ____                    _        ____
#    |  _ \  ___  _ __   __ _| | ___  |  _ \ _   _ _ __ ___  _ __   ___ _ __
#    | | | |/ _ \| '_ \ / _` | |/ _ \ | | | | | | | '_ ` _ \| '_ \ / _ \ '__|
#    | |_| | (_) | | | | (_| | |  __/ | |_| | |_| | | | | | | |_) |  __/ |
#    |____/ \___/|_| |_|\__, |_|\___| |____/ \__,_|_| |_| |_| .__/ \___|_|
#                       |___/                               |_|
# security dongle dumper for PlayStation2 based namco system 246/256

EE_BIN = DONGLE_DUMPER.ELF

EE_OBJS = $(addsuffix .o, main downloadfile ioprp usbd bdm bdmfs_fatfs usbmass_bd fileXio iomanX secrsif_mechaemu)

TTY = UDP
ifeq ($(TTY), PPC)
EE_OBJS += ppctty.o
EE_CFLAGS += -DTTY=1
else ifeq ($(TTY), UDP)
EE_OBJS += ps2dev9.o udptty_standalone.o
EE_CFLAGS += -DTTY=2
endif

EE_CFLAGS += -fdata-sections -ffunction-sections -DNEWLIB_PORT_AWARE
EE_LDFLAGS += -Wl,--gc-sections
EE_LIBS += -liopreboot -ldebug -lpatches -lfileXio -lcdvd

ifeq ($(DEBUG), 1)
  EE_CFLAGS += -DDEBUG -O0 -g
else
  EE_CFLAGS += -Os
  EE_LDFLAGS += -s
endif

all: $(EE_BIN)

clean:
	rm -rf $(EE_OBJS) $(EE_BIN)

ioprp.c: IOPRP_RETAIL.IMG
	bin2c $< $@ ioprp

vpath %.irx iop/
vpath %.irx $(PS2SDK)/iop/irx/
IRXTAG = $(notdir $(addsuffix _irx, $(basename $<)))
$(EE_OBJS_DIR)%.c: %.irx
	$(DIR_GUARD)
	@bin2c $< $@ $(IRXTAG)

# Include makefiles
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal