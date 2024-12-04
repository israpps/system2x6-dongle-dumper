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

#include <smem.h>
#include <smod.h>
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
#include "mechaemu_rpc.h"


static char pad_state[256] __attribute__((aligned(64)));
static int pad_buttons_raw = 0;
static int pad_buttons_current = 0;
static int pad_buttons_previous = 0;
int PollPadState(int port, int slot);

void get_Kc(const void *buffer, void *Kc);
void get_Kbit(const void *buffer, void *Kbit);
void hexdump (const char* name, unsigned char* buf, int size);


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
EXTERN_MODULE(padman_irx);
EXTERN_MODULE(mcman_irx);
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
#define INFORM(x) scr_setfontcolor(MODULE_OK(x.id, x.ret) ? 0x00cc00 : 0x0000cc);scr_printf(" %s: id:%d ret:%d - %-8s ", #x, x.id, x.ret, MODULE_OK(x.id, x.ret) ? "OK\r" : "ERR\n"); usleep(600000);
int loadusb();

smod_mod_info_t* GetIRXInfoByName(const char* name);
char ROMVER[15];
int loadmodulemc();
#define MCPORT 0
unsigned char Kbit[16], Kc[16];
unsigned char BKbit[16], BKc[16];
void scr_centerputs(const char* buf, char fillerbyte) {
    for (int x=0; x<(80-strlen(buf))/2;x++) scr_printf("%c", fillerbyte);
    scr_printf("%s", buf);
    for (int x=0; x<(80-strlen(buf))/2;x++) scr_printf("%c", fillerbyte);
}

int cardtest() {
    int ret = 0x0;
    for (int i = 0; i < 2; i++)
    {
        scr_setfontcolor(0xFFFFFF);
        scr_printf("\n\n\tmc%d: ", i );
        int a = 1;
        int mcformatted= MC_UNFORMATTED, mctype = sceMcTypeNoCard, mcfreeSpace = 0, ret;
        mcGetInfo(i, 0, &mctype, &mcfreeSpace, &mcformatted);
        mcSync(0, NULL, &ret);
        usleep(rand()%600000);


        if (mctype != sceMcTypePS2 ) {scr_setfontcolor(0x0000CC); a=0;}
        scr_printf("CardType:%d ", mctype);
        scr_setfontcolor(0xFFFFFF);
        //usleep(rand()%600000);
        scr_printf("FreeSpace:%04d ", mcfreeSpace );
        if (mcformatted != MC_FORMATTED) {scr_setfontcolor(0x0000CC); a=0;}
        //usleep(rand()%600000);
        scr_printf("Formatted:%d ", mcformatted);
        scr_setfontcolor(0xFFFFFF);
        scr_printf("McSync:%d\n", ret);
        if (a) {
            scr_setfontcolor(0x00FF00);
            scr_printf("\t\tThe card was successfully authenticated with developer magicgate\n");
            ret |= (1 << i);
        } else {
            scr_setfontcolor(0x0000CC);
            //if (ret == -11) scr_printf("\t\tNo memory card connected?\n");
            //else 
            scr_printf("\t\tCould not auth card with developer magicgate. card might not be OG\n");
        }

    }
    return ret;
}

int main(int argc, char** argv) {
    sio_puts("> mechaemu update binder\n> BuilDate: "__DATE__ " " __TIME__ "\n");
    while (!SifIopRebootBuffer(ioprp, size_ioprp)) {}; // install SECRMAN MECHAEMU
    sio_puts("> Waiting for SifIopSync()");
    memset(ROMVER, 0, sizeof(ROMVER));
    while (!SifIopSync()) {}; // wait for IOP to reboot
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
const char* title = " OG Card Tester ";
    scr_printf(" \n");
    scr_centerputs(title, '=');
    scr_printf("\tCoded by El_isra\n");
    //scr_printf("\thttps://github.com/israpps/system2x6-dongle-dumper\n");
    scr_printf("\tConsole ROMVER:        %s\n\n\n", ROMVER);
    //ModelNameInit();
    smod_mod_info_t* info = GetIRXInfoByName("secrman_nomecha");
    if (info == NULL) {
        scr_setfontcolor(0x0000CC);
        scr_printf("\n\n\tCannot replace SECRMAN.IRX: Aborting\n");
        goto brk;
        
    }
    if (loadmodulemc()==0) {
        scr_setfontcolor(0x00CCCC);
        scr_centerputs("Unplug and replug the cards before running the test to reset their IC", ' '); scr_printf("\n");
        scr_setfontcolor(0xFFFFFF);
        scr_centerputs("Press start to run test", ' ');
        while (1)
        {
            int pollInput = 1;
            while (pollInput != 0) {
                if (PollPadState(0, 0) != 0) {
                    pollInput = 0;
                    if ((pad_buttons_current & PAD_START) != 0) {
                        scr_clear();
                        scr_printf("\n");
                        scr_centerputs(title, '=');
                        scr_printf("\tCoded by El_isra\n");
                         scr_printf("\n\n");
                        cardtest();
                        scr_setfontcolor(0xFFFFFF);
                         scr_printf("\n\n");
                        sleep(1);
                        scr_centerputs("Press start to re-run test", ' ');
                        scr_centerputs("Press X to exit to OSDSYS", ' ');
                    } else if ((pad_buttons_current & PAD_CROSS) != 0) {
                        sleep(2);
                        goto brk_notime;
                    } else
                        pollInput = 1;
                }
            }
        }
    }
    brk:
    
        scr_setfontcolor(0xFFFFFF);
    scr_printf("\n\nProgram execution end. exiting to OSDSYS in 2 minutes\n");
    sleep(120);
    return 1;
    brk_notime:
    //while (!SifIopReset("", 0)) ;
    //while (!SifIopSync()) ;
    return 1;
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
    nocard:
    scr_setfontcolor(0xFFFFFF);
    return -1;
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
    scr_printf("\t%s:", name);
    for (int i = 0; i < size; i++)
    {
        scr_printf("%02X ", buf[i]);
    }
    scr_printf("\n");
    
}
//LIBCGLUE_SUPPORT_NAMCO_SYSTEM_2x6();


smod_mod_info_t* curr = NULL;
smod_mod_info_t* GetIRXInfoByName(const char* name) {
    smod_mod_info_t info;
    curr = NULL;
    char sName[21];
    int rv;
    while ((rv = smod_get_next_mod(curr, &info)) != 0) {
        curr = &info;
        if (curr == NULL) continue;
        smem_read(info.name, sName, 20);
        printf("%s: v%x\n", sName, info.version);
        sName[20] = 0;
        if (!strcmp(name, sName)) {
            return curr;
        }
    }
    return NULL;
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