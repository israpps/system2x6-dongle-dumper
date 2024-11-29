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
#include "mechaemu_rpc.h"
void get_Kc(const void *buffer, void *Kc);
void get_Kbit(const void *buffer, void *Kbit);
void hexdump (const char* name, unsigned char* buf, int size);


typedef struct {
    int id;
    int ret;
} modinfo_t;

modinfo_t sio2man, mcman, mcserv, usbd, bdm, fatfs, usbmass, genvmc, fileXio, iomanX, secrsif_mechaemu;
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

char ROMVER[15];
int loadmodulemc();
#define MCPORT 0
unsigned char Kbit[16], Kc[16];
unsigned char BKbit[16], BKc[16];
int main(int argc, char** argv) {
    sio_puts("> mechaemu update binder\n> BuilDate: "__DATE__ " " __TIME__ "\n");
    while (!SifIopRebootBuffer(ioprp, size_ioprp)) {}; // we need homebrew FILEIO
    sio_puts("> Waiting for SifIopSync()");
    while (!SifIopSync()) {}; // wait for IOP to reboot
    sio_puts("> startup services");
    SifInitIopHeap(); // Initialize SIF services for loading modules and files.
    SifLoadFileInit();
    fioInit();
    
    init_scr();
    scr_setCursor(0);
    sleep(2);
    sio_puts("> pull romver");
    memset(ROMVER, 0, sizeof(ROMVER));
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
    scr_printf(".\n\t ===== MECHAEMU Update binder =====\n");
    scr_printf("\tCoded by El_isra. genvmc module borrowed from OPL\n");
    //scr_printf("\thttps://github.com/israpps/system2x6-dongle-dumper\n");
    scr_printf("\tROMVER:        %s\n", ROMVER);
    //ModelNameInit();


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
        scr_printf("\n\tFailed to load fileXio. aborting dump...\n");
        goto brk;
    }
    if (loadmodulemc() == 0) {
        scr_setfontcolor(0xFFFFFF);
        scr_printf("\n"); 
        uint8_t* buf;
        const char* UNBOUND = "UNBOUND.KELF";
        const char* BOUND = "BOUND.KELF";
        int fd = open(UNBOUND, O_RDONLY);
        
        if (fd < 0) {
            scr_printf("\tcant open '%s' (%d %s)...\n", UNBOUND, fd, strerror(fd));
            goto brk;
        }
        int size = lseek(fd, 0, SEEK_END);
        scr_printf("\tKELF size is %d\n", size); 
        if (size < 0) {
            goto brk;
        }
        lseek(fd, 0, SEEK_SET);
        if ((buf = memalign(64, size)) != NULL) {
            if ((read(fd, buf, size)) != size) {
                close(fd);
                scr_printf("\tcannot read whole KELF: %d bytes\n", size); 
                goto brk;
            } else {
                get_Kbit(buf, Kbit);
                get_Kc(buf, Kc);
                scr_printf("Unbound: \n"); hexdump("Kbit", Kbit, 16); hexdump("Kc", Kc, 16);
                scr_printf("\tBinding update to memory card on mc%d:\n", MCPORT);
                int result = mechaemu_downloadfile(MCPORT + 2, 0, buf);
                if (result) {
                    scr_printf("\tBinding complete\n");
                    get_Kbit(buf, BKbit);
                    get_Kc(buf, BKc);
                    scr_printf("Bound: \n"); hexdump("Kbit", BKbit, 16); hexdump("Kc", BKc, 16);
                    scr_printf( "writing KELF to '%s'\n", BOUND);
                    int outfd = open(BOUND, O_WRONLY | O_CREAT | O_TRUNC);
                    if (outfd >= 0)
                    {
                        int written = write(outfd, buf, size);
                        if (written != size) {
                            scr_printf("\tI/O ERROR Writing output KELF\n");
                            result = -EIO;
                        }
                        close(outfd);
                    } else {
                        scr_printf("\tmechaemu_downloadfile(%d, 0): error\n", MCPORT);
                        result = -EIO;
                    }

                } else {
                    scr_printf("\tmechaemu_downloadfile(%d, 0): error\n", MCPORT);
                    goto brk;
                }
            }
        } else {
            close(fd);
            scr_printf("\tcannot allocate %d bytes\n", size);
            goto brk;
        }
    }
    brk:
    scr_printf("Program execution end. exiting to OSDSYS in 2 minutes\n");
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

    int mcformatted= MC_UNFORMATTED, mctype, mcfreeSpace, ret;
    mcGetInfo(MCPORT, 0, &mctype, &mcfreeSpace, &mcformatted);
    mcSync(0, NULL, &ret);

    scr_setfontcolor(0xFFFFFF);
    scr_printf("\tmc%d: ", MCPORT );
    if (mctype != sceMcTypePS2 ) scr_setfontcolor(0x0000CC);
    scr_printf("CardType:%d ", mctype );
    scr_setfontcolor(0xFFFFFF);
    scr_printf("FreeSpace:%d ", mcfreeSpace );
    if (mcformatted != MC_FORMATTED ) scr_setfontcolor(0x0000CC);
    scr_printf("Formatted:%d", mcformatted);
    scr_setfontcolor(0xFFFFFF);
    scr_printf("McSync:%d\n", ret);
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
//LIBCGLUE_SUPPORT_NAMCO_SYSTEM_2x6();