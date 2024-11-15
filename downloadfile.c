
#include <stdbool.h>
#include <tamtypes.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include "mechaemu_rpc.h"

#define DPRINTF(fmt, x...) printf(fmt, ##x)
#define CHECK_RPC_INIT() if (!rpc_initialized) {DPRINTF("ERROR: Cannot call %s if RPC server is not initialized\n", __FUNCTION__); return -2;}


static SifRpcClientData_t MechaEmuRPC;
static int rpc_initialized = false;


int mechaemu_init(void)
{
    int retries = 100;
    if (rpc_initialized)
        {return 0;}

    int E;
	while(retries--)
	{
		if((E = SifBindRpc(&MechaEmuRPC, MECHAEMU_RPC_IRX, 0)) < 0)
        {
            DPRINTF("Failed to bind RPC server for MECHAEMU (%d)\n", E);
			return SCE_EBINDMISS;
        }

		if(MechaEmuRPC.server != NULL)
			break;

		nopdelay();
	}

	rpc_initialized = retries;

	return (retries) ? 0 : ESRCH;
}

#define RPC_BUFPARAM(x) &x, sizeof(x)
int mechaemu_downloadfile(int port, int slot, void* KELFPointer)
{
    CHECK_RPC_INIT();

    struct DownLoadFileParam pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.port = port;
    pkt.slot = slot;
    memcpy(pkt.buffer, KELFPointer, sizeof(pkt.buffer)); //put 1kilobyte of the KELF into the RPC

    if (SifCallRpc(&MechaEmuRPC, SECRME_DOWNLOADFILE, 0, RPC_BUFPARAM(pkt), RPC_BUFPARAM(pkt), NULL, NULL) < 0)
    {
        DPRINTF("%s: RPC ERROR\n", __FUNCTION__);
        return -SCE_ECALLMISS;
    }
    if (pkt.result) memcpy(KELFPointer, pkt.buffer, sizeof(pkt.buffer)); //copy back the kilobyte from RPC to the original pointer, kbit and kc changed
    return pkt.result;
}
