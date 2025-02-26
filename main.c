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
#include <libmc.h>
#include <ps2sdkapi.h>
#include <string.h>
#include <sys/stat.h>
#include "genvmc.h"
#include "pad.h"


#include <smem.h>
#include <smod.h>
smod_mod_info_t* GetIRXInfoByName(const char* name);
int ListModules();
int dongledump(int port, const char* pathdump);

void genericgaugepercent(int percent);
void genericgauge (float progress);
void bottomgauge(int percent);
void ClearGauge(void);
int exist(char *filepath);

const char *ModelNameGet(void);
int ModelNameInit(void);
uint16_t getConsoleID();

typedef struct {
    int id;
    int ret;
} modinfo_t;

modinfo_t sio2man, mcman, padman, mcserv, usbd, bdm, fatfs, usbmass, genvmc, fileXio, iomanX;
#define EXTERN_MODULE(_irx) extern unsigned char _irx[]; extern unsigned int size_##_irx
EXTERN_MODULE(ioprp);
EXTERN_MODULE(usbd_irx);
EXTERN_MODULE(bdm_irx);
EXTERN_MODULE(bdmfs_fatfs_irx);
EXTERN_MODULE(usbmass_bd_irx);
EXTERN_MODULE(genvmc_irx);
EXTERN_MODULE(fileXio_irx);
EXTERN_MODULE(iomanX_irx);
EXTERN_MODULE(mcman_irx);
EXTERN_MODULE(padman_irx);
EXTERN_MODULE(mcserv_irx);
EXTERN_MODULE(sio2man_irx);

#define LOADMODULE(_irx, ret) SifExecModuleBuffer(&_irx, size_##_irx, 0, NULL, ret)
#define LOADMODULEFILE(path, ret) SifLoadStartModule(path, 0, NULL, ret)
#define MODULE_OK(id, ret) (id >= 0 && ret != 1)
#define INFORM(x) scr_setfontcolor(MODULE_OK(x.id, x.ret) ? 0x00cc00 : 0x0000cc);scr_printf("\t %-10s:(id:%d ret:%d) %-10s\r", #x, x.id, x.ret, MODULE_OK(x.id, x.ret) ? "OK" : "ERR")
int loadusb();

char ROMVER[15];
int loadmodulemc();
void scr_centerputs(const char* buf, char fillerbyte);
void PrintHeading();

#define mkdir_smart(path...) ((result = mkdir(path) >= 0) || result == -EEXIST)

int main(int argc, char** argv) {
    sio_puts("# dongle dumper start\n# BuilDate: "__DATE__ " " __TIME__ "\n");
    //while (!SifIopRebootBuffer(ioprp, size_ioprp)) {}; // replace SECRMAN
    sio_puts("# Waiting for SifIopSync()");
    //while (!SifIopSync()) {}; // wait for IOP to reboot
    sio_puts("# startup services");
    SifInitIopHeap(); // Initialize SIF services for loading modules and files.
    SifLoadFileInit();
    fioInit();
    //SifLoadStartModule("rom0:CDVDFSV", 0, NULL, NULL);
    init_scr();
    scr_setCursor(0);
    sio_puts("# pull romver");
    memset(ROMVER, 0, sizeof(ROMVER));
    GetRomName(ROMVER);
    scr_printf("\n\n\n\n");
    scr_centerputs(" security dongle dumper ", '=');
    scr_centerputs("Coded by El_isra. genvmc module borrowed from OPL", ' ');
    scr_centerputs("https://github.com/israpps/system2x6-dongle-dumper", ' ');
    ModelNameInit();
    scr_printf("\tConsole: %s (ROMVER:%s)\n", ModelNameGet(), ROMVER);
    sbv_patch_enable_lmb(); // patch modload to support SifExecModuleBuffer
    sbv_patch_disable_prefix_check(); // remove security from MODLOAD
    if (GetIRXInfoByName("secrman_nomecha") != NULL){
        sleep(4);
        scr_clear();
        scr_printf("\n\n\n\n");
        scr_setfontcolor(0x1111CC);
        scr_centerputs("FATAL ERROR", ' ');
        scr_centerputs("Could not replace SECRMAN.IRX", ' ');
        scr_setfontcolor(0xFFFFFF);
        scr_centerputs("report https://github.com/israpps/system2x6-dongle-dumper", ' ');
        scr_centerputs("--", '-');
        scr_printf("\nModules:\n");
        ListModules();
        goto tosleep;
    };
    if (!loadusb()) goto tosleep;
    
    iomanX.id = LOADMODULE(iomanX_irx, &iomanX.ret);
    INFORM(iomanX);
    fileXio.id = LOADMODULE(fileXio_irx, &fileXio.ret);
    INFORM(fileXio);
    if (MODULE_OK(fileXio.id, fileXio.ret)) {
        fileXioInit();
    } else {
        scr_printf("\tFailed to load fileXio. aborting dump...\n");
        goto tosleep;
    }
    if (loadmodulemc() == 0) {
        scr_setfontcolor(0xffffff);
    }
    mkdir("mass:/DONGLE_DUMPER/", 0755);
    int port = 0;
    int d=0;
    int dongcnt=0;
    scr_clear();
    PrintHeading();
    scr_printf("\tSTART: dump | SELECT: Exit | CIRCLE: Change slot\n");
    scr_printf("\tLEFT/RIGHT: Change dump file\n");
    scr_printf("\t Source    mc%d:\n", port);
    scr_printf("\t DumpFile: dongle-%d.bin\n", dongcnt);
    char fpath[128+1];
    while (1) {
        int x = 20;
        int PAD = ReadCombinedPadStatus();
        if (PAD ==0) {
            //hack: no pad pressed, jump to the waiter right away
        } else if (PAD & PAD_START) {
            d++;
            snprintf(fpath, 128, "mass:/DONGLE_DUMPER/dongle-%d.bin\n", dongcnt);
            if (dongledump(0, fpath) != 0)sleep(10);
        } else if (PAD & PAD_LEFT) {
            dongcnt--;
            if (dongcnt < 0) dongcnt = 100;
            d++;
        } else if (PAD & PAD_RIGHT) {
            dongcnt++;
            if (dongcnt > 100) dongcnt = 0;
            d++;
        } else if (PAD & PAD_SELECT) {
            return 0;
        } else if (PAD & PAD_CIRCLE) {
            port ^= 1;
            d++;
        }
        if (d) {
            scr_clear();
            PrintHeading();
            scr_printf("\tSTART: dump | SELECT: Exit | CIRCLE: Change slot\n");
            scr_printf("\tLEFT/RIGHT: Change dump file\n");
            scr_printf("\t Source    mc%d:\n", port);
            scr_printf("\t DumpFile: dongle-%d.bin\n", dongcnt);
            d=0;
        }
        while(--x);
    }
quit:
    sleep(120);
    return 0;
tosleep:
    SleepThread();
}
int dongledump(int port, const char* pathdump) {
    
    int ret;
    createVMCparam_t p;
    statusVMCparam_t vmc_stats;
    memset(&p, 0, sizeof(createVMCparam_t));
    const char* cardpath = pathdump;
    strcpy(p.VMC_filename, cardpath);
    p.VMC_card_slot = port; // 0=slot 1, 1=slot 2
    p.VMC_thread_priority = 0xF;
    scr_printf("\trequesting dump to '%s': ", cardpath);
    ret = fileXioDevctl("genvmc:", GENVMC_DEVCTL_CREATE_VMC, (void *)&p, sizeof(p), NULL, 0);
    if (ret == 0) {
        scr_printf("  OK\n");
    } else {
        scr_printf("  Error %d\n", ret);
        return -1;
    }
    sleep(1);
    memset(&vmc_stats, 0, sizeof(statusVMCparam_t));
    scr_printf("\twaiting VMC file creation...\n");
    scr_setfontcolor(0x009090);
    int x=0;
    while (1) {
        x = 20;
        ret = fileXioDevctl("genvmc:", GENVMC_DEVCTL_STATUS, NULL, 0, (void *)&vmc_stats, sizeof(vmc_stats));

        scr_setfontcolor(0xffffff);
        if (ret == 0) {
            scr_printf("\tStatus: %-30s\r", vmc_stats.VMC_msg);
            if (vmc_stats.VMC_progress > 20 && vmc_stats.VMC_progress < 50) scr_setfontcolor(0x00aaaa);
            if (vmc_stats.VMC_progress > 50 && vmc_stats.VMC_progress < 80) scr_setfontcolor(0x00aa00);
            if (vmc_stats.VMC_progress > 80) scr_setfontcolor(0x00ff00);
            bottomgauge(vmc_stats.VMC_progress);
            if (vmc_stats.VMC_status == GENVMC_STAT_AVAIL) {
                scr_printf("\n");
                break;
            }
        }

        while(--x);
    }
    scr_setfontcolor(0xffffff);
    ClearGauge();
    scr_printf("\n\tDone%-30s\n", "");
    scr_printf("\tVMC status = %d\n", vmc_stats.VMC_error);
    return 0;
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

    genvmc.id = LOADMODULE(genvmc_irx, &genvmc.ret);
    INFORM(genvmc);
    if (!MODULE_OK(genvmc.id, genvmc.ret)) {
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
    PadInitPads();
    return 0;
}


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
    scr_printf("\n\n\n");
    scr_centerputs(" security dongle dumper ", '=');
    scr_centerputs("coded by El_isra", ' ');
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

smod_mod_info_t* smod_curr = NULL;
smod_mod_info_t* GetIRXInfoByName(const char* name) {
    smod_mod_info_t info;
    smod_curr = NULL;
    char sName[21];
    int rv;
    while ((rv = smod_get_next_mod(smod_curr, &info)) != 0) {
        smod_curr = &info;
        if (smod_curr == NULL) continue;
        smem_read(info.name, sName, 20);
        //printf("%-21s:0x%x\n", sName, info.version);
        sName[20] = 0;
        if (!strcmp(name, sName)) {
            return smod_curr;
        }
    }
    return NULL;
}

int ListModules() {
    smod_mod_info_t info;
    smod_curr = NULL;
    char sName[21];
    int rv;
    int modc=0;
    int _found_secrman=0;
    while ((rv = smod_get_next_mod(smod_curr, &info)) != 0) {
        int found_secrman=0;
        smod_curr = &info;
        if (smod_curr == NULL) continue;
        smem_read(info.name, sName, 20);
        sName[20] = 0;
        if (!_found_secrman && (strstr(sName, "secrman") == NULL)){
            _found_secrman=1;
            found_secrman=1;
        } 
        if ((modc%2)==0) scr_printf("\n"); else scr_printf(" | ");
        if (found_secrman) scr_setfontcolor(0x00FFFF);
        scr_printf("%-21s:0x%x ", sName, info.version);
        if (found_secrman) scr_setfontcolor(0xFFFFFF);
        modc++;
    }
    scr_printf("\n");
    return modc;
}