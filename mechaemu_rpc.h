#ifndef MECHAEMU_RPC
#define MECHAEMU_RPC

#include <stdint.h>
#include <errno.h>

struct DownLoadFileParam
{
    int32_t port, slot;
    uint8_t buffer[0x400];

    int32_t result;
};

#define MECHAEMU_RPC_IRX  (0x10245) // 0x10000 + `M` `E` `C` `H` `A` `E` `M` `U`
#define SECRME_DOWNLOADFILE (0x358) // 0x100   + `D` `O` `W` `N` `L` `O` `A` `D`



int mechaemu_init(void);
int mechaemu_downloadfile(int port, int slot, void* KELFPointer);

#endif
