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

MEMORY
{
	FLASH	(rx):	ORIGIN = 0x00000000, LENGTH = 0x00040000
	RAM		(rwx):	ORIGIN = 0x20000000, LENGTH = 0x00008000
	STACK	(rwx):	ORIGIN = 0x20007FFF, LENGTH = 0x00000000
}

/*
 * Section definitions:
 *
 * .text 		- machine instructions.
 * .data 		- initialized data defined in the program.
 * .bss  		- un-initialized global and static variables (to be initialized to 0 before starting main).
 * .stack		- just contains the pointer to the stack end at the right place.
 */
SECTIONS
{
	.text :
	{
		_start_text = .;
		KEEP(*(.nvic_table))
		*(.text.*)
		*(.rodata.*)
		_end_text = .;
	} >FLASH

	.data :
	{
		_start_data = .;
		*(.data.*)
		*(vtable)
		_end_data = .;
	} >RAM AT >FLASH

	.bss :
	{
		_start_bss = .;
		*(.bss.*)
		*(COMMON)
		_end_bss = .;
	} >RAM

	.stack :
	{
		_stack_top = .;
	} >STACK
}

