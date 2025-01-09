/**
 *      ____                    _        ____                                  
 *     |  _ \  ___  _ __   __ _| | ___  |  _ \ _   _ _ __ ___  _ __   ___ _ __ 
 *     | | | |/ _ \| '_ \ / _` | |/ _ \ | | | | | | | '_ ` _ \| '_ \ / _ \ '__|
 *     | |_| | (_) | | | | (_| | |  __/ | |_| | |_| | | | | | | |_) |  __/ |   
 *     |____/ \___/|_| |_|\__, |_|\___| |____/ \__,_|_| |_| |_| .__/ \___|_|   
 *                        |___/                               |_|              
 *  PlayStation 2 Arcade dongle dumper 
 *  Copyright (c) 2024 Matias Israelson - MIT license
 */

#include <kernel.h>
#include <stdio.h>
#include <iopheap.h>
#include <rom0_info.h>
#include <fileio.h>
#include <fileXio_rpc.h>
#include <iopcontrol.h>
#include <iopcontrol_special.h>
#include <loadfile.h>
#include <sio.h>
#include <debug.h>
#include <sbv_patches.h>
#include <ps2sdkapi.h>
#include <string.h>
#include <stdlib.h>
#include <libsecr-common.h>
#include <malloc.h>
#include <sys/stat.h>
#include <libmc.h>
#include <libpad.h>
#include <errno.h>
#include "mechaemu_rpc.h"

static char pad_state[256] __attribute__((aligned(64)));
static int pad_buttons_raw = 0;
static int pad_buttons_current = 0;
static int pad_buttons_previous = 0;
int PollPadState(int port, int slot);

void get_Kc(const void *buffer, void *Kc);
void get_Kbit(const void *buffer, void *Kbit);
void hexdump (const char* name, unsigned char* buf, int size);

int exist(char *filepath);

typedef struct {
    int id;
    int ret;
} modinfo_t;

modinfo_t sio2man, mcman, mcserv, padman, usbd, bdm, fatfs, usbmass, genvmc, fileXio, iomanX, secrsif_mechaemu;
#define EXTERN_MODULE(_irx) extern unsigned char _irx[]; extern unsigned int size_##_irx
EXTERN_MODULE(ioprp);
EXTERN_MODULE(usbd_irx);
EXTERN_MODULE(bdm_irx);
EXTERN_MODULE(bdmfs_fatfs_irx);
EXTERN_MODULE(usbmass_bd_irx);
EXTERN_MODULE(genvmc_irx);
EXTERN_MODULE(fileXio_irx);
EXTERN_MODULE(iomanX_irx);
EXTERN_MODULE(sio2man_irx);
EXTERN_MODULE(mcman_irx);
EXTERN_MODULE(padman_irx);
EXTERN_MODULE(mcserv_irx);
EXTERN_MODULE(secrsif_mechaemu_irx);
#if TTY == 1
EXTERN_MODULE(ppctty_irx);
#elif TTY == 2
EXTERN_MODULE(ps2dev9_irx);
EXTERN_MODULE(udptty_standalone_irx);
#endif
#define LOADMODULE(_irx, ret) SifExecModuleBuffer(&_irx, size_##_irx, 0, NULL, ret)
#define LOADMODULEFILE(path, ret) SifLoadStartModule(path, 0, NULL, ret)
#define MODULE_OK(id, ret) (id >= 0 && ret != 1)
#define INFORM(x) scr_setfontcolor(MODULE_OK(x.id, x.ret) ? 0x00cc00 : 0x0000cc);scr_printf(" %s: id:%d ret:%d - %-8s ", #x, x.id, x.ret, MODULE_OK(x.id, x.ret) ? "OK\r" : "ERR\n"); //usleep(600000);
int loadusb();

char ROMVER[15];
int loadmodulemc();
#define MCPORT 0
unsigned char Kbit[16], Kc[16];
unsigned char BKbit[16], BKc[16];

void scr_fillhalf(int size, char filler) {
    for (int x=0; x<(80-size)/2;x++) scr_printf("%c", filler);
}

void scr_centerputs(const char* buf, char fillerbyte) {
    scr_fillhalf(strlen(buf), fillerbyte);
    scr_printf("%s", buf);
    scr_fillhalf(strlen(buf), fillerbyte);
    if(strlen(buf) % 2 != 0) scr_printf("\n");
}

void PrintHeading() {
    scr_printf("\n\n");
    scr_centerputs(" MECHAEMU Update binder ", '=');
    scr_centerputs("coded by El_isra", ' ');
}
void genericgaugepercent(int percent);
void genericgauge (float progress);
void bottomgauge(int percent);
void ClearGauge(void);

const char* UNBOUND = "boot.kelf";
char* BOUND = "mc0:boot.bin";
int BindKelf(int port, const char* input, const char* output) {
    int result;
    BOUND[2] = '0' + port;
    scr_setfontcolor(0xFFFFFF);
    scr_printf("\n"); 
    uint8_t* buf = NULL;
    bottomgauge(0);
    
    int is_ok = 1;
    scr_printf("Checking card.");
    int mcformatted = MC_UNFORMATTED, mctype = 0, mcfreeSpace = 0, ret;
    mcGetInfo(port, 0, &mctype, &mcfreeSpace, &mcformatted);
    bottomgauge(10);
    scr_printf(".");
    mcSync(0, NULL, &ret);
    scr_printf(".\n");
    if (mctype != sceMcTypePS2 ) is_ok = 0;
    if (mcformatted != MC_FORMATTED ) is_ok = 0;
    scr_printf("\tmc%d: %d,%d,%d,%d %s\n", port , mctype, mcfreeSpace, mcformatted, ret, (is_ok) ? "OK" : "ERR");
    if (!is_ok) {
        scr_printf("\tError detecting dongle!\n");
        return ENOENT;
    }
    bottomgauge(20);
    int fd = open(input, O_RDONLY);
    
    if (fd < 0) {
        scr_printf("\tcant open '%s' (%d %s)...\n", input, fd, strerror(fd));
        return ENOENT;
    }
    int size = lseek(fd, 0, SEEK_END);
    scr_printf("\tKELF size is %d\n", size); 
    if (size < 0 || size >= ((mcfreeSpace+2)*1024)) {
        scr_printf("\tNot enough space on dongle! kelfsize:%d  CardSpace:%d\n", size, ((mcfreeSpace+2)*1024));
        return EINVAL;
    }
    lseek(fd, 0, SEEK_SET);
    if ((buf = memalign(64, size)) != NULL) {
        bottomgauge(30);
        if ((read(fd, buf, size)) != size) {
            close(fd);
            scr_printf("\tI/O ERROR. Cannot read input KELF\n"); 
            result = EIO;
        } else {
            bottomgauge(40);
            get_Kbit(buf, Kbit);
            get_Kc(buf, Kc);
            scr_printf("Unbound: \n"); 
            scr_setfontcolor(0x00FFFF); hexdump("Kbit", Kbit, 16); hexdump("Kc", Kc, 16); scr_setfontcolor(0xFFFFFF);
            scr_printf("\tBinding update to security Dongle on mc%d:\n", port);
            bottomgauge(50);
            result = mechaemu_downloadfile(port + 2, 0, buf);
            bottomgauge(60);
            if (result) {
                scr_printf("\tBinding complete\n");
                get_Kbit(buf, BKbit);
                get_Kc(buf, BKc);
                scr_printf("Bound:   \n"); scr_setfontcolor(0x00FFFF); hexdump("Kbit", BKbit, 16); hexdump("Kc", BKc, 16); scr_setfontcolor(0xFFFFFF);
                scr_printf( "writing KELF to '%s'\n", output);
                int outfd = open(output, O_WRONLY | O_CREAT | O_TRUNC);
                if (outfd >= 0)
                {
                    bottomgauge(70);
                    int written = write(outfd, buf, size);
                    if (written != size) {
                        scr_printf("\tI/O ERROR Writing output KELF\n");
                        result = EIO;
                    } else {bottomgauge(80); scr_printf("\tSuccess!");}
                    close(outfd);
                    bottomgauge(90);
                } else {
                    scr_printf("\tCannot open output path %d\n", outfd);
                    result = EIO;
                }
            } else {
                scr_printf("\tmechaemu_downloadfile(%d, 0): error\n", port);
                result = EINVAL;
            }
        }
    } else {
        close(fd);
        scr_printf("\tcannot allocate %d bytes\n", size);
        result = ENOMEM;
    }
	if (buf) free(buf);
    bottomgauge(100);
    return result;
}

int main(int argc, char** argv) {
    sio_puts("> mechaemu update binder\n> BuilDate: "__DATE__ " " __TIME__ "\n");
    while (!SifIopRebootBuffer(ioprp, size_ioprp)) {}; // replace SECRMAN with ours
    sio_puts("> Waiting for SifIopSync()");
    memset(ROMVER, 0, sizeof(ROMVER)); // to squeze boot time. code that does not depend on IOP goes here
    while (!SifIopSync()) {}; // wait for IOP to be ready
    sio_puts("> startup services");
    SifInitIopHeap(); // Initialize SIF services for loading modules and files.
    SifLoadFileInit();
    fioInit();
    
    init_scr();
    scr_setCursor(0);
    sleep(2);
    sio_puts("> pull romver");
    GetRomName(ROMVER);
    //scr_printf("\tConsole model: %s\n", ModelNameGet());
    //scr_printf("\tConsole ID:    0x%x\n", getConsoleID());
    //scr_printf("\tMachineType:   %04i\n", MachineType());
    sbv_patch_enable_lmb(); // patch modload to support SifExecModuleBuffer
    sbv_patch_disable_prefix_check(); // remove security from MODLOAD
    
#if TTY == 1
    sio_puts("> PPCTTY Startup");
LOADMODULE(ppctty_irx, NULL);
#elif TTY == 2
    sio_puts("> UDPTTY Startup");
LOADMODULE(ps2dev9_irx, NULL);
LOADMODULE(udptty_standalone_irx, NULL);
#endif
    PrintHeading();
    //scr_printf("\thttps://github.com/israpps/system2x6-dongle-dumper\n");
    scr_printf("\tROMVER:        %s\n", ROMVER);
    //ModelNameInit();
    for (int i = 0; i < 100; i += rand()%10)
    {
        bottomgauge(i);
        usleep(rand()%400000);
    }
    

    if (!loadusb()) goto tosleep;
    
    secrsif_mechaemu.id = LOADMODULE(secrsif_mechaemu_irx, &secrsif_mechaemu.ret);
    INFORM(secrsif_mechaemu);
    if (mechaemu_init()) {
        scr_printf("\tCannot connect to secrsif_mechaemu.irx\n");
        goto brk;
    }
    iomanX.id = LOADMODULE(iomanX_irx, &iomanX.ret);
    INFORM(iomanX);
    fileXio.id = LOADMODULE(fileXio_irx, &fileXio.ret);
    INFORM(fileXio);
    if (MODULE_OK(fileXio.id, fileXio.ret)) {
        scr_printf("\nConnecting to filexio.irx...\r");
        fileXioInit();
    } else {
        scr_printf("\n\tFailed to load fileXio. aborting...\n");
        goto brk;
    }
    if (loadmodulemc() == 0) {
        int scrc = 1, port = 0;
        while (1)
        {
            if (scrc) {
                const char* fm = "target: 'mc%d:'\n";
                scr_clear(); scr_setfontcolor(0xFFFFFF);
                PrintHeading();
                scr_centerputs("START: Bind Update | SELECT: Exit program", ' ');
                scr_centerputs("Press O to Change target card slot", ' ');
                scr_centerputs("--", '-');
                scr_fillhalf(strlen(fm), ' '); scr_printf("  ");scr_printf(fm, port);
                scrc = 0;
            }
            int pollInput = 1;
            while (pollInput != 0) {
                if (PollPadState(0, 0) != 0) {
                    pollInput = 0;
                    if ((pad_buttons_current & PAD_START) != 0) {
                        if (BindKelf(port, UNBOUND, BOUND) != 0) sleep(4);
                         sleep(5);
                        scrc = 1;
                    } else if ((pad_buttons_current & PAD_CIRCLE) != 0) {
                        port ^= 1;
                        scrc = 1;
                    } else if ((pad_buttons_current & PAD_SELECT) != 0) {
                        sleep(2);
                        goto brk_notime;
                    } else
                        pollInput = 1;
                }
            }
        }
    }
    brk:
    scr_printf("Program execution end. exiting to OSDSYS in 2 minutes\n");
    sleep(120);
    brk_notime:
    return 0;
tosleep:
    SleepThread();
}

int loadusb() {
    usbd.id = LOADMODULE(usbd_irx, &usbd.ret);
    INFORM(usbd);
    bdm.id = LOADMODULE(bdm_irx, &bdm.ret);
    INFORM(bdm);
    fatfs.id = LOADMODULE(bdmfs_fatfs_irx, &fatfs.ret);
    INFORM(fatfs);
    usbmass.id = LOADMODULE(usbmass_bd_irx, &usbmass.ret);
    INFORM(usbmass);
    sleep(3);
    scr_setfontcolor(0xdddddd);
    
    struct stat buffer;
    int ret = -1;
    int retries = 0;

    while (ret != 0 && retries <= 50) {
        ret = stat("mass:/", &buffer);
        /* Wait until the device is ready */
        nopdelay();

        retries++;
    }
    if (ret != 0) {
        scr_printf("\t- error: 'mass:/' not found (%d)\n", ret);
        return 0;
    } else {
        scr_printf("\t- found 'mass:/' after %d attempt%c\n", retries, (retries > 1) ? 's' : ' ');
        return 1;
    }
}

int loadmodulemc() {
    sio2man.id = LOADMODULE(sio2man_irx, &sio2man.ret);
    INFORM(sio2man);
    if (!MODULE_OK(sio2man.id, sio2man.ret)) {
        return -1;
    }
    
    mcman.id =   LOADMODULE(mcman_irx, &mcman.ret);
    INFORM(mcman);
    if (!MODULE_OK(mcman.id, mcman.ret)) {
        return -1;
    }
    
    mcserv.id =  LOADMODULE(mcserv_irx, &mcserv.ret);
    INFORM(mcserv);
    if (!MODULE_OK(mcserv.id, mcserv.ret)) {
        return -1;
    }
    mcInit(MC_TYPE_XMC);
    padman.id = LOADMODULE(padman_irx, &padman.ret);
    INFORM(padman);
    if (!MODULE_OK(padman.id, padman.ret)) {
        return -1;
    }
    padInit(0);
    int ret;
    if ((ret = padPortOpen(0, 0, pad_state)) == 0) {
        // Failed to open pad port.
        scr_printf("Failed to open pad port 0: %d\n", ret);
        return -1;
    }
    return 0;
}


// 0x00002b20
void get_Kbit(const void *buffer, void *Kbit)
{
    const SecrKELFHeader_t *header = buffer;
    int offset                     = sizeof(SecrKELFHeader_t);
    u8 *kbit_offset;

    if (header->BIT_count > 0)
        offset += header->BIT_count * sizeof(SecrBitBlockData_t); // They used a loop for this. D:
    if (((header->flags) & 1) != 0)
        offset += ((unsigned char *)buffer)[offset] + 1;
    if (((header->flags) & 0xF000) == 0)
        offset += 8;

    kbit_offset = (u8 *)buffer + offset;
    memcpy(Kbit, (void *)kbit_offset, 16);
    
}

// 0x00002c80
void get_Kc(const void *buffer, void *Kc)
{
    const SecrKELFHeader_t *header = buffer;
    int offset                     = sizeof(SecrKELFHeader_t);
    u8 *kc_offset;

    if (header->BIT_count > 0)
        offset += header->BIT_count * sizeof(SecrBitBlockData_t); // They used a loop for this. D:
    if (((header->flags) & 1) != 0)
        offset += ((unsigned char *)buffer)[offset] + 1;
    if (((header->flags) & 0xF000) == 0)
        offset += 8;

    kc_offset = (u8 *)buffer + offset + 0x10; // Goes after Kbit
    memcpy(Kc, (void *)kc_offset, 16);
    
}

void hexdump (const char* name, unsigned char* buf, int size) {
    scr_printf("\t%-5s:", name);
    for (int i = 0; i < size; i++)
    {
        scr_printf("%02X ", buf[i]);
    }
    scr_printf("\n");
    
}

// DMA buffer for pad input state:
int PollPadState(int port, int slot)
{
    struct padButtonStatus buttons;
    // Wait until the pad is ready.
    int state = padGetState(port, slot);
    while (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1 && state != PAD_STATE_DISCONN) {
        // Retry polling...
        state = padGetState(port, slot);
    }
    // Get pad input state.
    state = padRead(0, 0, &buttons);
    if (state != 0) {
        // Update button state.
        pad_buttons_raw = 0xFFFF ^ buttons.btns;
        pad_buttons_current = pad_buttons_raw & ~pad_buttons_previous;
        pad_buttons_previous = pad_buttons_raw;
    }
    return state;
}


int exist(char *filepath)
{
    if (filepath == NULL)
        return 0;
    int fdn;

    fdn = open(filepath, O_RDONLY);
    if (fdn < 0)
        return 0;

    close(fdn);

    return 1;
}

void genericgauge (float progress)
{
    int barWidth = 70;

    scr_printf("[");
    int pos = barWidth * progress;
    for (int i = 0; i < barWidth; ++i)
	{
	  if (i < pos)
        scr_printf("=");
	  else if (i == pos)
        scr_printf(">");
	  else
        scr_printf(" ");
	}
    
    scr_printf("]\r");
}

//percentage represented on signed integer. values from 0-100
void genericgaugepercent(int percent) {
    genericgauge(percent*0.01);
}

#define GAUGELINE 25
void bottomgauge(int percent) {
    int X = scr_getX(), Y = scr_getY();
    scr_setXY(0, GAUGELINE);
    scr_setfontcolor(0xFFFFFF);
    genericgauge(percent*0.01);
    scr_setXY(X, Y);
}
void ClearGauge(void) {
    scr_clearline(GAUGELINE);
}