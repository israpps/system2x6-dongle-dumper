#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <kernel.h>
#include <sifcmd.h>
#include <libcdvd-common.h>
#include <libcdvd.h>
#include <unistd.h>
#include <debug.h>

#define MODEL_NAME_MAX_LEN 17
static char ModelName[MODEL_NAME_MAX_LEN];

extern char ROMVER[];
uint16_t ModelId = 0;
uint8_t mechaver[4] = {0,0,0,0};
int got_mechaver = 0, got_modelid = 0;
static void GuessConsoleModelByID();

//for some reason not part of the libcdvd code
int sceCdRM(char *ModelName, u32 *stat)
{
    unsigned char rdata[9];
    unsigned char sdata;
    int result1, result2;

    sdata = 0;
    result1 = sceCdApplySCmd(0x17, &sdata, 1, rdata);
    // result1 = sceCdApplySCmd(0x17, &sdata, 1, rdata, 9);

    *stat = rdata[0];
    memcpy(ModelName, &rdata[1], 8);

    sdata = 8;
    result2 = sceCdApplySCmd(0x17, &sdata, 1, rdata);
    // result2 = sceCdApplySCmd(0x17, &sdata, 1, rdata, 9);

    *stat |= rdata[0];
    memcpy(&ModelName[8], &rdata[1], 8);

    return ((result1 != 0 && result2 != 0) ? 1 : 0);
}

static int ReadModelName(char *name)
{
    int result;
    u32 stat;

    /*  This function is a hybrid between the late ROM browser program and the HDD Browser.
        In v2.20, there was only a simple null-terminate before calling sceCdRM(), as below.
        However, this does not support the early PlayStation 2 models.
        In the HDD Browser, the model name buffer was a global instead of an argument.    */
    memset(name, 0, MODEL_NAME_MAX_LEN);

    /*      Invoking the "Read model" command on the first SCPH-10000 seems to return an error code,
            but it seems to function on my SCPH-15000...although no valid name is returned!
            In the HDD OSD, "SCPH-10000" is returned when the ROM v1.00 is detected (It checks rom0:ROMVER).
            It attempts to read the model name from the ROM OSDSYS program, when ROM v1.01 (late SCPH-10000 and all SCPH-15000 models) is detected.

            The model name was originally hardcoded into the OSDSYS. The MECHACON EEPROM did not have the model name recorded in it.

            Oddly, the console models that come with v1.01, like the SCPH-15000 and DTL-H10000,
            will always be identified as "SCPH-10000" by their ROM OSDSYS programs.
    */
#define CHECK_CONSOLE_IS_NOT_COH (ROMVER[4] != 'T' && ROMVER[5] != 'Z')
    if ((ROMVER[0] == '0' && ROMVER[1] == '1' && ROMVER[2] == '0') && CHECK_CONSOLE_IS_NOT_COH) {
#ifdef PROTOKERNEL_MODELNAME_SONY_STYLE_GUESS
        if (ROMVER[3] == '0') // For ROM v1.00 (Early SCPH-10000 units).
            strcpy(name, "SCPH-10000");
        else { // For ROM v1.01 (Late SCPH-10000, and all SCPH-15000 units).
            int fd;
            if ((fd = open("rom0:OSDSYS", O_RDONLY)) >= 0) { // The model name is located at this address.
                lseek(fd, 0x8C808, SEEK_SET);
                read(fd, name, MODEL_NAME_MAX_LEN);
                close(fd);
            } else
                strcpy(name, "Unknown");
        }
#else
    GuessConsoleModelByID();
#endif
        return 0; // Original returned -1
    } else {
        if ((result = sceCdRM(name, &stat)) == 1) { // Command issued successfully.
            if (stat & 0x80)
                return -2;
            if ((stat & 0x40) || name[0] == '\0')
                GuessConsoleModelByID();

            return 0; // Original returned -1
        } else
            return -2;
    }
}

int ModelNameInit(void)
{
    return ReadModelName(ModelName);
}

const char *ModelNameGet(void)
{
    return ModelName;
}


char ReadNVM(uint16_t offset, uint16_t *data);
int getMechaVersion(uint8_t *data) {
    if (got_mechaver) return 1;
	u8 input[1];
	u8 output[16];
	input[0] = 0x00;
	if (sceCdApplySCmd(0x03, input, sizeof(input), output) != 1)
	{
		return 0;
	}
    got_mechaver = 1;
	memcpy(data, &output[0], 3);
	
	return 1;
}

uint16_t getConsoleID() {
    if (!getMechaVersion(mechaver)) return 0;
    if (!got_modelid) {
        if (mechaver[1] < 4)
            ReadNVM(0xE4, &ModelId);
        else
            ReadNVM(0xF8, &ModelId);
        got_modelid = 1;
    }
    return ModelId;
}

static void GuessConsoleModelByID() {
    switch (getConsoleID())
    {
    case 0xd209:
        strcpy(ModelName, "COH-H30000");
        break;
    case 0xd20b:
        strcpy(ModelName, "COH-H31000");
        break;
    case 0xd20e:
        strcpy(ModelName, "COH-H31100");
        break;
    
    default:
        strcpy(ModelName, "unknown");
        break;
    }
}

char ReadNVM(uint16_t offset, uint16_t *data)
{
	u8 input[2];
	u8 output[16];
	input[0] = offset >> 8;
	input[1] = offset;
	if (sceCdApplySCmd(0x0A, input, sizeof(input), output) != 1 || output[0] != 0)
	{
		scr_printf("Failed to send mechacon cmd 0x0A!\n");
		return 0;
	}
	
	*data = output[1];
	*data <<= 8;
	*data |= output[2];
	
	return 1;
}
