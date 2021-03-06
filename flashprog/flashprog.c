/*
 * This file is part of SPI Flash Programmer (SPIFP)
 * Copyright © 2014 Rachel Mant (dx-mon@users.sourceforge.net)
 *
 * SPIFP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SPIFP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "strUtils.h"
#include "USB.h"
#include "USBInterface.h"

#ifdef _MSC_VER
#define _usleep _sleep
#else
#define MSECS_IN_SEC 1000
#define NSECS_IN_MSEC 1000000
#define _usleep(milisec) \
	{\
		struct timespec req = {milisec / MSECS_IN_SEC, (milisec % MSECS_IN_SEC) * NSECS_IN_MSEC}; \
		nanosleep(&req, NULL); \
	}
#endif

/*
 * USB transfer protocol:
 *
 * CMD_START + 4 bytes => uint32_t length of data total
 * CMD_PAGE + 1 bytes + up to 256 bytes => uint8_t page length (0 == 256), page data
 * CMD_ABORT => Sent to indicate user requested to abort
 * CMD_STOP => Sent at the end of transfering all the data to indicate we think we've finished.
 *   Device replies with some data indicating the status of the flash device and if there are any remaining expected bytes.
 *
 * After sending each command, including CMD_STOP, the device must respond with the command code and a byte indicating whether
 * it could execute it correctly - 1 for OK, 0 for error.
 */
int dataFD;
size_t dataLen;

/* Reserve enough space for a page of data */
unsigned char data[256];

static const uint8_t numProgChars = 4;
static const char *progressChars = "|/-\\";
uint8_t progChar;

int usage(char *prog)
{
	printf("Usage:\n"
		"\t%s binfile.bin\n", prog);
	return 1;
}

void tick()
{
	printf("%c\b", progressChars[progChar]);
	fflush(stdout);
	progChar = ++progChar % numProgChars;
}

void processFile()
{
	int32_t res, blockLen = -1;
	uint32_t pageNum = 0;
	progChar = 0;
	printf("Programming: ");
	while (blockLen != 0)
	{
		blockLen = read(dataFD, data, 256);
		if (blockLen == -1)
		{
			usbWriteByte(CMD_ABORT);
			die("\rError: read() returned an error.. cannot continue..\n");
		}
		else if (blockLen == 0)
			continue;
		usbWriteByte(CMD_PAGE);
		usbWriteByte(blockLen & 0xFF);
		usbWrite(data, blockLen);
		res = usbRead(data, 2);
		if (res != 2 || data[0] != CMD_PAGE || data[1] != RPL_OK)
		{
			printf("\rError: Programming a data page failed\n");
			break;
		}
		else
		{
			pageNum++;
			if ((pageNum % 4) == 0)
				tick();
		}
	}
}

void waitForErase()
{
	int32_t res;
	progChar = 0;
	printf("Erasing: ");
	fflush(stdout);
	do
	{
		usbWriteByte(CMD_ERASE);
		res = usbRead(data, 2);
		if (res != 2 || data[0] != CMD_ERASE)
			die("\rError: Erase cycle interrupted, cannot continue..\n");
		else if (data[1] == RPL_OK)
			break;
		tick();
		_usleep(100);
	}
	while (data[1] == RPL_BUSY);
	printf("Done!\n");
}

void writeLength()
{
	data[0] = (dataLen >> 24) & 0xFF;
	data[1] = (dataLen >> 16) & 0xFF;
	data[2] = (dataLen >> 8) & 0xFF;
	data[3] = dataLen & 0xFF;
	usbWrite(data, 4);
}

int main(int argc, char **argv)
{
	int32_t res;
	struct stat dataStat;

	if (argc != 2)
		return usage(argv[0]);
	usbInit();
	dataFD = open(argv[1], O_RDONLY | O_EXCL);
	if (dataFD == -1)
	{
		usbDeinit();
		die("Error: Could not open the file specified\n");
	}
	if (stat(argv[1], &dataStat) != 0)
	{
		close(dataFD);
		usbDeinit();
		die("Error: Could not determine the size of the file specified\n");
	}
	dataLen = dataStat.st_size;

	// Send the start command + 4 bytes indicating how long the data file is
	usbWriteByte(CMD_START);
	writeLength();
	// Now wait for the return code
	res = usbRead(data, 2);
	if (res != 2 || data[0] != CMD_START || data[1] != RPL_OK)
		printf("Tiva C Launchpad said it could not start a transfer\n");
	else
	{
		waitForErase();
		processFile();
		usbWriteByte(CMD_STOP);
		res = usbRead(data, 6);
		if (res != 6 || data[4] != CMD_STOP || data[5] != RPL_OK)
			printf("\rTiva C Launchpad encountered errors during programming, please try again\n");
		else if (*((uint32_t *)data) != 0)
			printf("\rTiva C Launchpad did not receieve whole file\n");
		else
			printf("Done!\n");
	}

	close(dataFD);
	usbDeinit();
	return 0;
}
