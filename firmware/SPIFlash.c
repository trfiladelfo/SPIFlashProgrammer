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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <tm4c123gh6pm.h>
#ifndef NOCONFIG
#include "config.h"
#endif

#define WREN	0x06
#define WRDI	0x04
#define RDIDL	0x9F
#define RDIDS	0x9E
#define RDSR	0x05
#define WRSR	0x01
#define WRLR	0xE5
#define RDLR	0xE8
#define READ	0x03
#define PP		0x02
#define SSE		0x20
#define SE		0xD8
#define BE		0xC7

/*
 * 0x20 => Manufacturer ID (Numonyx)
 * 0x71 => Memory Type (SPI Flash)
 * 0x14 => Memory Capacity - 4Mbit
 */
static const uint8_t expectedDID[3] = { 0x20, 0x71, 0x14 };

int datacmp(const uint8_t *a, const uint8_t *b, const size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
	{
		if (*a != *b)
			return *a - *b;
		a++;
		b++;
	}
	return 0;
}

void writeSPI(uint8_t data)
{
	uint8_t temp;
	SSI0_DR_R = data;
	while ((SSI0_SR_R & 0x01) != 1);
	temp = SSI0_DR_R & 0xFF;
}

uint8_t readSPI()
{
	SSI0_DR_R = 0;
	while ((SSI0_SR_R & 0x04) != 4);
	return SSI0_DR_R & 0xFF;
}

bool verifyDID()
{
	uint8_t data[3], i;
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	/* Send a short DID read request */
	writeSPI(RDIDS);
	/* For each of the three bytes returned */
	for (i = 0; i < 3; i++)
		/* And having recieved the answer, buffer it */
		data[i] = readSPI();
	/* Deselect the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
	/* Compare the recieved data to the expected data */
	/* I wanted to use memcmp here, but could not make use of the newlib implemenation */
	return datacmp(data, expectedDID, 3) == 0;
}

void unlockDevice()
{
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	/* Write enable and write the Status Register */
	writeSPI(WREN);
	writeSPI(WRSR);
	/* All zeros disables all enabled protections */
	writeSPI(0x00);
	/* Deselect the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
}

void lockDevice()
{
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	/* Write enable and write the Status Register */
	writeSPI(WREN);
	writeSPI(WRSR);
	/* The three Block Protect bits are bits [4:2] */
	writeSPI(0x1C);
	/* Deselect the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
}

void eraseDevice()
{
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	/* Arm the device (Write Enable) and issue erase instruction */
	writeSPI(WREN);
	writeSPI(BE);
	/* Deselect the device - executes erase */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
}

void writeData(const uint8_t sector, const uint8_t page, const uint8_t *data, const uint16_t dataLen)
{
	uint16_t i;
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	/* Write enable the device */
	writeSPI(WREN);
	/* And issue the Page Program instruction */
	writeSPI(PP);
	/* Sector is (sector >> 4), and sub-sector is (sector & 0x0F) */
	writeSPI(sector);
	/* The page of the sector */
	writeSPI(page);
	/* Must be 0 to select the first byte of the page */
	writeSPI(0);
	/* Send the data */
	for (i = 0; i < dataLen; i++)
		writeSPI(data[i]);
	/* Deselect the device - executes write instruction */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
}

bool verifyData(const uint8_t *data, const size_t dataLen)
{
	uint16_t i;
	bool ok = true;
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	writeSPI(READ);
	writeSPI(0);
	writeSPI(0);
	writeSPI(0);
#ifndef NOCONFIG
	for (i = 0; i < dataLen; i++)
	{
		if (readSPI() != data[i]);
		{
			ok = false;
			break;
		}
	}
#endif
	/* Deselect the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
	return ok;
}

void waitWriteComplete()
{
	/* Select the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 0;
	/* Write the Read Status Register instruction */
	writeSPI(RDSR);
	/* And use it's continuous read mode till the write is complete (bit 7 => 0) */
	while ((readSPI() & 7) != 0);
	/* Deselect the device */
	GPIO_PORTA_DATA_BITS_R[0x08] = 8;
}

void transferBitfile(const void *data, const size_t dataLen)
{
	uint16_t addr, pages;
	const uint8_t *dataPtr = data;

	if (!verifyDID())
		return;

	pages = (dataLen >> 8) + ((dataLen & 0xFF) != 0 ? 1 : 0);
	unlockDevice();
	eraseDevice();
	waitWriteComplete();
	for (addr = 0; addr < pages; addr++)
	{
		if ((addr + 1) < (dataLen >> 8))
			writeData(addr >> 8, addr & 0xFF, dataPtr, 256);
		else
			writeData(addr >> 8, addr & 0xFF, dataPtr, dataLen & 0xFF);
		waitWriteComplete();
#ifndef NOCONFIG
		if (data == config)
			dataPtr += 256;
#endif
	}
	lockDevice();
	if (verifyData(data, dataLen))
		GPIO_PORTF_DATA_BITS_R[0x0E] = 0x08;
}

int main()
{
	/* Enable ports A and F */
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R0 | SYSCTL_RCGCGPIO_R5;
	/* Enable SSI0 */
	SYSCTL_RCGCSSI_R |= SYSCTL_RCGCSSI_R0;
	/* Enable Timer 0 */
	SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R0;
	/* Wait for the ports to come online */
	while ((SYSCTL_PRGPIO_R & (SYSCTL_PRGPIO_R0 | SYSCTL_PRGPIO_R5)) != (SYSCTL_PRGPIO_R0 | SYSCTL_PRGPIO_R5));
	/* Port F is protected, so enable changing it to digital GPIO */
	GPIO_PORTF_LOCK_R = GPIO_LOCK_KEY;
	GPIO_PORTF_CR_R |= 0x01;
	GPIO_PORTF_LOCK_R = 0;
	/* Set the ports to digital mode */
	GPIO_PORTA_DEN_R |= 0x3C;
	GPIO_PORTF_DEN_R |= 0x1F;
	/* Wait for SSI0 to come online */
	while ((SYSCTL_PRSSI_R & SYSCTL_PRSSI_R0) != SYSCTL_PRSSI_R0);
	/* Wait for Timer 1 to come online */
	while ((SYSCTL_PRTIMER_R & SYSCTL_PRTIMER_R0) != SYSCTL_PRTIMER_R0);

	/* Ensure the LED and switch pins on Port F are straight GPIO */
	GPIO_PORTF_AFSEL_R &= ~0x1F;
	/* Enable the LED pin outputs */
	GPIO_PORTF_DIR_R = 0x0E;
	/* Set pullups on the 2 buttons */
	GPIO_PORTF_DR2R_R |= 0x11;
	GPIO_PORTF_PUR_R |= 0x11;
	/* Set the blue LED on */
	GPIO_PORTF_DATA_BITS_R[0x0E] = 0x04;

	/* Configure the SSI (SPI) pins as alternative function and enable their use by the SPI module */
	GPIO_PORTA_AFSEL_R = 0x34;
	GPIO_PORTA_PCTL_R |= GPIO_PCTL_PA5_SSI0TX | GPIO_PCTL_PA4_SSI0RX |
		/*GPIO_PCTL_PA3_SSI0FSS | */GPIO_PCTL_PA2_SSI0CLK;
	SSI0_CR1_R = 0;
	GPIO_PORTA_DR2R_R |= 0x3C;
	/* Pull the !CS pin high with a 2mA drive strength */
	GPIO_PORTA_PUR_R |= 0x08;
	/* Pull the Rx, Tx and Clk pins low with a 2mA drive strength */
	GPIO_PORTA_PDR_R |= 0x34;
	/* Set the !CS pin to an output */
	GPIO_PORTA_DIR_R |= 0x08;
	/* Set Freescale SPI, SPO = 1, SPH = 1 */
	SSI0_CR0_R = SSI_CR0_SPO | SSI_CR0_SPH | SSI_CR0_FRF_MOTO | SSI_CR0_DSS_8;
	/* We have a 16 MHz clock, and we interface to the SPI Flash chip at 8MHz */
	SSI0_CC_R = 0;
	/* Scale the clock by 2 to make it 8MHz */
	SSI0_CPSR_R = 2;
	/* Enable the interface */
	SSI0_CR1_R = SSI_CR1_SSE;

	/* Configure the Timer 0 module for submodule A operation */
	TIMER0_CTL_R &= ~TIMER_CTL_TAEN;
	TIMER0_TAMR_R = TIMER_TAMR_TAMR_1_SHOT | TIMER_TAMR_TACDIR | TIMER_TAMR_TAMIE;
	/* Set the timeout for 500us */
	TIMER0_TAMATCHR_R = 8000000;
	TIMER0_ICR_R = TIMER_ICR_TAMCINT;

	while (1)
	{
#ifndef NOCONFIG
		/* If either button has been pressed.. */
		if (GPIO_PORTF_DATA_BITS_R[0x11] != 0x11)
		{
			/* Stop the timer if it's already running */
			TIMER0_CTL_R &= ~TIMER_CTL_TAEN;
			/* Set the LED to red for busy/processing */
			GPIO_PORTF_DATA_BITS_R[0x0E] = 0x02;
			/* Attempt the transfer */
			transferBitfile(config, configLen);
			/* Reset the timer and set it running */
			TIMER0_TAV_R = 0;
			TIMER0_CTL_R |= TIMER_CTL_TAEN;
		}
#endif
		/* If the timer has triggered the Match event */
		if ((TIMER0_RIS_R & TIMER_RIS_TAMRIS) != 0)
		{
			/* Reset the LED back to blue for idle */
			GPIO_PORTF_DATA_BITS_R[0x0E] = 0x04;
			/* And stop the timer while resetting the interrupt flag for match */
			TIMER0_CTL_R &= ~TIMER_CTL_TAEN;
			TIMER0_ICR_R = TIMER_ICR_TAMCINT;
		}
	}

	return 0;
}
