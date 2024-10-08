#include <kernel.h>
#include <stdio.h>
#include <iopcontrol.h>
#include <iopcontrol_special.h>
#include <loadfile.h>
#include <sio.h>
#include <debug.h>
#include <sbv_patches.h>
#include <ps2sdkapi.h>
#include <string.h>
#include <sys/stat.h>
#include "genvmc.h"
const char *ModelNameGet(void);
int ModelNameInit(void);
uint16_t getConsoleID();

typedef struct {
    int id;
    int ret;
} modinfo_t;

modinfo_t sio2man, mcman, mcserv, usbd, bdm, fatfs, usbmass, genvmc, fileXio, iomanX;
#define EXTERN_MODULE(_irx) extern unsigned char _irx[]; extern unsigned int size_##_irx
EXTERN_MODULE(ioprp);
EXTERN_MODULE(usbd_irx);
EXTERN_MODULE(bdm_irx);
EXTERN_MODULE(bdmfs_fatfs_irx);
EXTERN_MODULE(usbmass_bd_irx);
EXTERN_MODULE(genvmc_irx);
EXTERN_MODULE(fileXio_irx);
EXTERN_MODULE(iomanX_irx);
#define LOADMODULE(_irx, ret) SifExecModuleBuffer(&_irx, size_##_irx, 0, NULL, ret)
#define LOADMODULEFILE(path, ret) SifLoadStartModule(path, 0, NULL, ret)
#define MODULE_OK(id, ret) (id >= 0 && ret != 1)
#define INFORM(x) scr_setfontcolor(MODULE_OK(x.id, x.ret) ? 0x00cc00 : 0x0000cc);scr_printf("\t %s: id:%d ret:%d (%s)\n", #x, x.id, x.ret, MODULE_OK(x.id, x.ret) ? "OK" : "ERR")
int loadusb();

char ROMVER[15];

int main(int argc, char** argv) {
    sio_puts("# dongle dumper start\n# BuilDate: "__DATE__ " " __TIME__ "\n");
    while (!SifIopRebootBuffer(ioprp, size_ioprp)) {}; // we need homebrew FILEIO
    sio_puts("# Waiting for SifIopSync()");
    while (!SifIopSync()) {}; // wait for IOP to reboot
    sio_puts("# startup services");
    SifInitIopHeap(); // Initialize SIF services for loading modules and files.
    SifLoadFileInit();
    fioInit();
    SifLoadStartModule("rom0:CDVDFSV", 0, NULL, NULL);
    init_scr();
    scr_setCursor(0);
    sio_puts("# pull romver");
    memset(ROMVER, 0, sizeof(ROMVER));
    GetRomName(ROMVER);
    scr_printf(".\n\t ===== Namco System 246/256 security dongle dumper =====\n");
    scr_printf("\tCoded by El_isra. genvmc module borrowed from OPL\n");
    scr_printf("\tROMVER:        %s\n", ROMVER);
    ModelNameInit();
    scr_printf("\tConsole model: %s\n", ModelNameGet());
    scr_printf("\tConsole ID:    0x%x\n", getConsoleID());
    scr_printf("\tMachineType:   %04i\n", MachineType());
    sbv_patch_enable_lmb(); // patch modload to support SifExecModuleBuffer
    sbv_patch_disable_prefix_check(); // remove security from MODLOAD

    if (!(ROMVER[4] == 'T' && ROMVER[5] == 'Z')) {
        scr_setfontcolor(0x0000FF);
        scr_printf("\tthis PS2 is NOT a namco system 246.\n\taborting...\n");
        goto tosleep;
    }

    scr_printf("\tLoading usb modules:\n");
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
        int ret;
        createVMCparam_t p;
        statusVMCparam_t vmc_stats;
        memset(&p, 0, sizeof(createVMCparam_t));
        const char* cardpath = "mass:COH-H10020.bin";
        strcpy(p.VMC_filename, cardpath);
        p.VMC_card_slot = 0; // 0=slot 1, 1=slot 2
        p.VMC_thread_priority = 0xf;
        scr_printf("\trequesting dump to '%s': ", cardpath);
        ret = fileXioDevctl("genvmc:", GENVMC_DEVCTL_CREATE_VMC, (void *)&p, sizeof(p), NULL, 0);
        if (ret == 0) {
            scr_printf("  OK\n");
        } else {
            scr_printf("  Error %d\n", ret);
            goto tosleep;
        }

        memset(&vmc_stats, 0, sizeof(statusVMCparam_t));
        scr_printf("\twaiting VMC file creation...\n");
        scr_setfontcolor(0x009090);
        int x=0;
        while (1) {
            x = 20;
            ret = fileXioDevctl("genvmc:", GENVMC_DEVCTL_STATUS, NULL, 0, (void *)&vmc_stats, sizeof(vmc_stats));
            if (vmc_stats.VMC_progress > 20 && vmc_stats.VMC_progress < 50) scr_setfontcolor(0x00aaaa);
            if (vmc_stats.VMC_progress > 50 && vmc_stats.VMC_progress < 80) scr_setfontcolor(0x00aa00);
            if (vmc_stats.VMC_progress > 80) scr_setfontcolor(0x00ff00);
            if (ret == 0) {
                scr_printf("\tprogress: %d: %-30s\r", vmc_stats.VMC_progress, vmc_stats.VMC_msg);
                if (vmc_stats.VMC_status == GENVMC_STAT_AVAIL) {
                    scr_printf("\n");
                    break;
                }
            }

            while(--x);
        }
        scr_setfontcolor(0xffffff);
        scr_printf("\n\tDone%-30s\n", "");
        scr_printf("\tVMC status = %d\n", vmc_stats.VMC_error);
    }
    sleep(120);
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
    int id, ret;
    //sio2man.id = LOADMODULEFILE("mass:/SIO2MAN", &sio2man.ret);
    //if (!MODULE_OK(sio2man.id, sio2man.ret))
        sio2man.id = LOADMODULEFILE("rom0:SIO2MAN", &sio2man.ret);
    INFORM(sio2man);
    if (!MODULE_OK(sio2man.id, sio2man.ret)) {
        return -1;
    }
    //mcman.id =   LOADMODULEFILE("mass:/DONGLEMAN", &mcman.ret);
    //if (!MODULE_OK(mcman.id, mcman.ret))
        mcman.id =   LOADMODULEFILE("rom0:MCMAN", &mcman.ret);
    INFORM(mcman);
    if (!MODULE_OK(mcman.id, mcman.ret)) {
        return -1;
    }
    //mcserv.id =  LOADMODULEFILE("mass:/MCSERV", &mcserv.ret);
    //if (!MODULE_OK(mcserv.id, mcserv.ret))
        mcserv.id =  LOADMODULEFILE("rom0:MCSERV", &mcserv.ret);
    INFORM(mcserv);
    if (!MODULE_OK(mcserv.id, mcserv.ret)) {
        return -1;
    }
    genvmc.id =  LOADMODULE(genvmc_irx, &genvmc.ret); // modified genvmc module that auths card with I_McDetectCard2. this should reset the watchdog before begining dump
    INFORM(genvmc);
    if (!MODULE_OK(genvmc.id, genvmc.ret)) {
        return -1;
    }
    return 0;
}

LIBCGLUE_SUPPORT_NAMCO_SYSTEM_2x6();