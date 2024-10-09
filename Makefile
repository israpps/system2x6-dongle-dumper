#     ____                    _        ____
#    |  _ \  ___  _ __   __ _| | ___  |  _ \ _   _ _ __ ___  _ __   ___ _ __
#    | | | |/ _ \| '_ \ / _` | |/ _ \ | | | | | | | '_ ` _ \| '_ \ / _ \ '__|
#    | |_| | (_) | | | | (_| | |  __/ | |_| | |_| | | | | | | |_) |  __/ |
#    |____/ \___/|_| |_|\__, |_|\___| |____/ \__,_|_| |_| |_| .__/ \___|_|
#                       |___/                               |_|
# security dongle dumper for PlayStation2 based namco system 246/256

EE_BIN = DONGLE_DUMPER.ELF

EE_OBJS = main.o modelname.o ioprp.o \
	usbd.o bdm.o bdmfs_fatfs.o usbmass_bd.o genvmc.o fileXio.o iomanX.o

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

ioprp.img:
	wget https://github.com/israpps/wLaunchELF_ISR/raw/system-2x6-support/iop/__precompiled/IOPRP_FILEIO.IMG -O $@

%.c: %.img
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