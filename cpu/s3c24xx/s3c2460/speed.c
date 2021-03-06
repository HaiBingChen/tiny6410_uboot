/*
 * (C) Copyright 2001-2004
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * (C) Copyright 2002
 * David Mueller, ELSOFT AG, d.mueller@elsoft.ch
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* This code should work for both the S3C2400 and the S3C2410
 * as they seem to have the same PLL and clock machinery inside.
 * The different address mapping is handled by the s3c24xx.h files below.
 */

#include <common.h>
#include <s3c2460.h>

#define MPLL 0
#define UPLL 1

/* ------------------------------------------------------------------------- */
/* NOTE: This describes the proper use of this file.
 *
 * CONFIG_SYS_CLK_FREQ should be defined as the input frequency of the PLL.
 *
 * get_FCLK(), get_HCLK(), get_PCLK() and get_UCLK() return the clock of
 * the specified bus in HZ.
 */
/* ------------------------------------------------------------------------- */

static ulong get_PLLCLK(int pllreg)
{
	ulong r, m, p, s;

	if (pllreg == MPLL)
		r = MPLLCON_REG;
	else if (pllreg == UPLL)
		r = UPLLCON_REG;
	else
		hang();

	m = ((r>>16) & 0xff) + 8;
	p = ((r>>8) & 0x3f) + 2;
	s = r & 0x3;

	return ((CONFIG_SYS_CLK_FREQ * m) / (p << s));
}

/* return ARMCORE frequency */
ulong get_ARMCLK(void)
{
	return ((CLKDIV_REG & 0x00010000) ? get_FCLK()/2 : get_FCLK());
}

/* return FCLK frequency */
ulong get_FCLK(void)
{
	return (get_PLLCLK(MPLL));
}

/* return HCLK frequency */
ulong get_HCLK(void)
{
	return (get_FCLK()/((CLKDIV_REG & 0x3)+1));
}

/* return PCLK frequency */
ulong get_PCLK(void)
{
	return ((CLKDIV_REG & 0x4) ? get_HCLK()/2 : get_HCLK());
}

/* return UCLK frequency */
ulong get_UCLK(void)
{
	return (get_PLLCLK(UPLL));
}

int print_cpuinfo(void)
{
	printf("\nCPU:   S3C2460A@%dMHz\n", get_ARMCLK()/1000000);
	printf("       Fclk = %dMHz, Hclk = %dMHz, Pclk = %dMHz\n",
			get_FCLK()/1000000, get_HCLK()/1000000, get_PCLK()/1000000);

	return 0;
}
