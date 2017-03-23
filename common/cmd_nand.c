/*
 * Driver for NAND support, Rick Bronson
 * borrowed heavily from:
 * (c) 1999 Machine Vision Holdings, Inc.
 * (c) 1999, 2000 David Woodhouse <dwmw2@infradead.org>
 *
 * Added 16-bit nand support
 * (C) 2004 Texas Instruments
 */

#include <common.h>


#ifndef CFG_NAND_LEGACY
/*
 *
 * New NAND support
 *
 */
#include <common.h>

#if (CONFIG_COMMANDS & CFG_CMD_NAND)

#include <command.h>
#include <watchdog.h>
#include <malloc.h>
#include <asm/byteorder.h>

#ifdef CONFIG_SHOW_BOOT_PROGRESS
# include <status_led.h>
# define SHOW_BOOT_PROGRESS(arg)	show_boot_progress(arg)
#else
# define SHOW_BOOT_PROGRESS(arg)
#endif

#include <jffs2/jffs2.h>
#include <nand.h>

#if (CONFIG_COMMANDS & CFG_CMD_JFFS2) && defined(CONFIG_JFFS2_CMDLINE)

/* parition handling routines */
int mtdparts_init(void);
int id_parse(const char *id, const char **ret_id, u8 *dev_type, u8 *dev_num);
int find_dev_and_part(const char *id, struct mtd_device **dev,
		u8 *part_num, struct part_info **part);
#endif

extern nand_info_t nand_info[];       /* info for NAND chips */

static int nand_dump_oob(nand_info_t *nand, ulong off)
{
	return 0;
}

static int nand_dump_mlc(ulong off)
{
	int i;
	u_char *buf, *p;
	unsigned int block, page;
	extern int NAND_ReadSectorWithOOB(unsigned block, unsigned sector, unsigned char *buffer, unsigned char *oob);

	if (off % NandPageSizeInByte != 0) {
		puts("offset should be multiple of page size\n");
		return -1;
	}
	
	buf = malloc(NandPageSizeInByte + NandOobSize);
	
	if (!buf) {
		puts("No memory for page buffer\n");
		return 1;
	}

	page = (off % NandBlockSizeInByte) / NandPageSizeInByte;
	block = off / NandBlockSizeInByte;

	NAND_ReadSectorWithOOB(block, page, buf, buf + NandPageSizeInByte);

	printf("Page %08x dump:\n", off);

	i = NandPageSizeInByte >> 4;
	p = buf;

	while (i--) {
		printf( "\t%02x %02x %02x %02x %02x %02x %02x %02x"
			"  %02x %02x %02x %02x %02x %02x %02x %02x\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
		p += 16;
	}

	puts("OOB:\n");

	p = buf + NandPageSizeInByte;
	i = NandOobSize >> 3;

	while (i--) {
		printf( "\t%02x %02x %02x %02x %02x %02x %02x %02x\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		p += 8;
	}

	free(buf);

	return 0;
}

/* ------------------------------------------------------------------------- */

static inline int str2long(char *p, ulong *num)
{
	char *endptr;

	*num = simple_strtoul(p, &endptr, 16);
	return (*p != '\0' && *endptr == '\0') ? 1 : 0;
}

static int
arg_off_size(int argc, char *argv[], nand_info_t *nand, ulong *off, ulong *size)
{
	int idx = nand_curr_device;
#if (CONFIG_COMMANDS & CFG_CMD_JFFS2) && defined(CONFIG_JFFS2_CMDLINE)
	struct mtd_device *dev;
	struct part_info *part;
	u8 pnum;

	if (argc >= 1 && !(str2long(argv[0], off))) {
		if ((mtdparts_init() == 0) &&
		    (find_dev_and_part(argv[0], &dev, &pnum, &part) == 0)) {
			if (dev->id->type != MTD_DEV_TYPE_NAND) {
				puts("not a NAND device\n");
				return -1;
			}
			*off = part->offset;
			if (argc >= 2) {
				if (!(str2long(argv[1], size))) {
					printf("'%s' is not a number\n", argv[1]);
					return -1;
				}
				if (*size > part->size)
					*size = part->size;
			} else {
				*size = part->size;
			}
			idx = dev->id->num;
			*nand = nand_info[idx];
			goto out;
		}
	}
#endif

	if (argc >= 1) {
		if (!(str2long(argv[0], off))) {
			printf("'%s' is not a number\n", argv[0]);
			return -1;
		}
	} else {
		*off = 0;
	}

	if (argc >= 2) {
		if (!(str2long(argv[1], size))) {
			printf("'%s' is not a number\n", argv[1]);
			return -1;
		}
	} else {
		*size = nand->size - *off;
	}

#if (CONFIG_COMMANDS & CFG_CMD_JFFS2) && defined(CONFIG_JFFS2_CMDLINE)
out:
#endif
	printf("device %d ", idx);
	if (*size == nand->size)
		puts("whole chip\n");
	else
		printf("offset 0x%x, size 0x%x\n", *off, *size);
	return 0;
}

unsigned int FriendlyARMGetNandSizeInMB(void)
{
	nand_info_t *nand;
	if (nand_curr_device < 0 || nand_curr_device >= CFG_MAX_NAND_DEVICE ||
	    !nand_info[nand_curr_device].name) {
		return 0;
	}
	nand = &nand_info[nand_curr_device];
	return 1<<(NandSizeInByteShift - 20);
}

int FriendlyARMReadNand(unsigned char *data, unsigned len, unsigned offset)
{
	unsigned block;
	unsigned page;
	unsigned count;
	unsigned last_block = (1<<(NandSizeInByteShift - NandBlockSizeInByteShift)) - 1;

	unsigned checked_block = (unsigned)-1;

	if (offset % NandPageSizeInByte != 0) {
		return -1;
	}
	block = offset / NandBlockSizeInByte;
	page =  (offset % NandBlockSizeInByte) / NandPageSizeInByte;
	for (count = 0; count < len; count += NandPageSizeInByte) {
		if (checked_block != block) {
			// found a good block
			for(;;) {
				if (block > last_block) {
					return -2;
				}
				if (NAND_CheckBlock(block)) {
					checked_block = block;
					break;
				}
				block++;
			}
		}
		if (!NAND_ReadPage(block, page, data + count) ) {
			return -4;
		}
		page++;
		if (page >= NandBlockSizeInByte/NandPageSizeInByte) {
			page = 0;
			block++;
			printf(".");
		}
	}
	return 0;
}

int FriendlyARMFormat(int StartBlock, int EndBlock, int lowformat)
{
	int i;
	int OK;
	
	for (i = StartBlock; i <= EndBlock; i++) {
		if (!NAND_CheckBlock(i)) {
			if (lowformat) {
				NAND_EraseBlock(i);
			}
		} else {
			OK = NAND_EraseBlock(i);
			if (!OK) {
				return -1;
			}
		}
	}

	return 0;
}

int FriendlyARMFormatFrom(int StartBlock, int lowformat)
{
	int LastBlock = (1 << (NandSizeInByteShift - NandBlockSizeInByteShift)) - 1;
	int ret = FriendlyARMFormat(StartBlock, LastBlock, lowformat);
	return ret;
}

#if !defined(YaffsImageOobSizePerPage)
#define YaffsImageOobSizePerPage 64
#endif

static int WriteOneBlock(unsigned char *buffer, unsigned int size, int StartBlock, int EndBlock, int *ResultBlock, int WithOOB)
{
	int i;
Top:

	// found a good block
	for (;;) {
		if (StartBlock > EndBlock) {
			return 0;
		}
		if (NAND_CheckBlock(StartBlock)) {
			break;
		}
		printf("Skip block 0x%x\n", StartBlock);
		StartBlock++;
	}
	*ResultBlock = StartBlock;
	
	for (i = 0; i < 10; i++) {
		int OK = 0;
		unsigned s;
		unsigned Step = WithOOB ? (NandPageSizeInByte + YaffsImageOobSizePerPage) : NandPageSizeInByte;
		
		// write each sector
		for (s = 0; s < size; s += Step) {
			unsigned sector = s / Step;
			unsigned char *data = buffer + s;

			static unsigned char write_oob[640];
			
			if (!WithOOB) {
				memset(write_oob, 0xFF, sizeof write_oob);
			} else {
				memcpy(write_oob, data + NandPageSizeInByte, sizeof write_oob);
			}
			
			OK = NAND_WriteSectorWithOOB(StartBlock, sector, data, write_oob);
			if (!OK) {
				break;
			}
		}
		if (OK) {
			return 1;
		}
		//
		printf("write block 0x%x error, erase and try again\n", StartBlock);
		NAND_EraseBlock(StartBlock);
	}
	printf("mark block 0x%x bad\n", StartBlock);
	NAND_MarkBlockBad(StartBlock);
	goto Top;
	
	return 0;
}

static int FriendlyARMWriteNandMlc2(unsigned char *data, unsigned len, unsigned offset, unsigned MaxNandSize)
{
	int IsYaffs = 0;
	int EndBlock;
	int block;
	int Step;
	unsigned int i;
	int OK = 1;

	if (offset % NandBlockSizeInByte != 0) {
		return -1;
	}
	
	block = offset / NandBlockSizeInByte;

	if (MaxNandSize == (unsigned)(-1)) {
		if (NandIsMlc()) {
			// not support MLC Yaffs
			return -2;
		}
		IsYaffs = 1;
		MaxNandSize = (1 << NandSizeInByteShift) - offset;
	}
	if (MaxNandSize == (unsigned)(-2)) { // UBIFS
		MaxNandSize = (1 << NandSizeInByteShift) - offset;
	}
	Step = NandBlockSizeInByte;
	EndBlock = (offset + MaxNandSize + NandBlockSizeInByte - 1) / (NandBlockSizeInByte) - 1;
	len = ( len + NandPageSizeInByte - 1) / NandPageSizeInByte * NandPageSizeInByte;
	
	FriendlyARMFormat(block, EndBlock, 0);
	
	for (i = 0; i < len; i += Step) {
		unsigned int size;

		size = len - i;
		if (size > Step) {
			size = Step;
		}
		
		OK = WriteOneBlock(data + i, size, block, EndBlock, &block, IsYaffs);
		if (!OK) {
			return -3;
		}
		block++;
	}
	
	return 0;
}

int FriendlyARMWriteNand(const unsigned char*data, unsigned len, unsigned offset, unsigned MaxNandSize)
{
	nand_info_t *nand;
	int ret;
	int i;
	int IsYaffs;

	if (NandIsMlc()) {
		ret = FriendlyARMWriteNandMlc2((unsigned char *)data, len, offset, MaxNandSize);
		return ret;
	}
	IsYaffs = MaxNandSize == (unsigned)(-1);

	if (nand_curr_device < 0 || nand_curr_device >= CFG_MAX_NAND_DEVICE ||
	    !nand_info[nand_curr_device].name) {
		printf("\nno devices available\n");
		return -1;
	}
	nand = &nand_info[nand_curr_device];
	if (offset % (128 * 1024) != 0) {
		return -2;
	}

	if (IsYaffs) {
		if (len % (2048 + 64) != 0) {
			return -5;
		}
		MaxNandSize = nand->size - offset;
	} else {
		len = ((len - 1) / (128 * 1024) + 1) * (128 * 1024);
	}

	// try 3 times
	for (i = 0; i < 3; i++) {
		nand_erase_options_t e_opts;
		nand_write_options_t w_opts;

		// erase
		memset(&e_opts, 0, sizeof e_opts);
		e_opts.offset = offset;
		e_opts.length = MaxNandSize;
		ret = nand_erase_opts(nand, &e_opts);
		ret = 0;
		if (ret != 0) {
			ret = -3;
			continue;
		}

		// write
		memset(&w_opts, 0, sizeof(w_opts));
		w_opts.buffer	  = (unsigned char *)data ;
		w_opts.length	  = len;
		w_opts.offset	  = offset;
		w_opts.blockalign = 1;
		w_opts.pad 	  = ! IsYaffs;
		w_opts.autoplace  = !!IsYaffs;
		w_opts.writeoob   = !!IsYaffs;
		ret = nand_write_opts(nand, &w_opts);
		if (ret != 0) {
			ret = -4;
			continue;
		}

		// passed, go away
		break;
	}
	return ret;
}

int do_nand(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	int i, dev, ret;
	ulong addr, off, size;
	char *cmd, *s;
	nand_info_t *nand;
	int quiet = 0;
	const char *quiet_str = getenv("quiet");

	/* at least two arguments please */
	if (argc < 2)
		goto usage;

	if (quiet_str)
		quiet = simple_strtoul(quiet_str, NULL, 0) != 0;

	cmd = argv[1];

	if (strcmp(cmd, "info") == 0) {

		putc('\n');
		for (i = 0; i < CFG_MAX_NAND_DEVICE; i++) {
			if (nand_info[i].name)
				printf("Device %d: %s, sector size %lu KiB\n",
					i, nand_info[i].name,
					NandBlockSizeInByte >> 10);
		}
		return 0;
	}

	if (strcmp(cmd, "device") == 0) {

		if (argc < 3) {
			if ((nand_curr_device < 0) ||
			    (nand_curr_device >= CFG_MAX_NAND_DEVICE))
				puts("\nno devices available\n");
			else
				printf("\nDevice %d: %s\n", nand_curr_device,
					nand_info[nand_curr_device].name);
			return 0;
		}
		dev = (int)simple_strtoul(argv[2], NULL, 10);
		if (dev < 0 || dev >= CFG_MAX_NAND_DEVICE || !nand_info[dev].name) {
			puts("No such device\n");
			return 1;
		}
		printf("Device %d: %s", dev, nand_info[dev].name);
		puts("... is now current device\n");
		nand_curr_device = dev;

#ifdef CFG_NAND_SELECT_DEVICE
		/*
		 * Select the chip in the board/cpu specific driver
		 */
		board_nand_select_device(nand_info[dev].priv, dev);
#endif

		return 0;
	}

	if (strcmp(cmd, "bad") != 0 && strcmp(cmd, "erase") != 0 &&
	    strncmp(cmd, "dump", 4) != 0 &&
	    strncmp(cmd, "read", 4) != 0 && strncmp(cmd, "write", 5) != 0 &&
	    strcmp(cmd, "scrub") != 0 && strcmp(cmd, "markbad") != 0 &&
	    strcmp(cmd, "biterr") != 0 &&
	    strcmp(cmd, "lock") != 0 && strcmp(cmd, "unlock") != 0 )
		goto usage;

	/* the following commands operate on the current device */
	if (nand_curr_device < 0 || nand_curr_device >= CFG_MAX_NAND_DEVICE ||
	    !nand_info[nand_curr_device].name) {
		puts("\nno devices available\n");
		return 1;
	}
	nand = &nand_info[nand_curr_device];

	if (strcmp(cmd, "bad") == 0) {
		printf("\nDevice %d bad blocks:\n", nand_curr_device);
		if (NandIsMlc()) {
			unsigned int block_num = (1<<(NandSizeInByteShift - NandBlockSizeInByteShift));
			unsigned int i;
			for (i = 0; i < block_num; i++) {
				if (!NAND_CheckBlock(i)) {
					printf("  %08X\n", i<<NandBlockSizeInByteShift);
				}
			}
		
		} else {
			for (off = 0; off < nand->size; off += nand->erasesize)
				if (nand_block_isbad(nand, off))
					printf("  %08x\n", off);
		}
		return 0;
	}

	/*
	 * Syntax is:
	 *   0    1     2       3    4
	 *   nand erase [clean] [off size]
	 */
	if (strcmp(cmd, "erase") == 0 || strcmp(cmd, "scrub") == 0) {
		nand_erase_options_t opts;
		/* "clean" at index 2 means request to write cleanmarker */
		int clean = argc > 2 && !strcmp("clean", argv[2]);
		int o = clean ? 3 : 2;
		int scrub = !strcmp(cmd, "scrub");

		printf("\nNAND %s: ", scrub ? "scrub" : "erase");
		/* skip first two or three arguments, look for offset and size */
		if (arg_off_size(argc - o, argv + o, nand, &off, &size) != 0)
			return 1;

		memset(&opts, 0, sizeof(opts));
		opts.offset = off;
		opts.length = size;
		opts.jffs2  = clean;
		opts.quiet  = quiet;

		if (scrub) {
			puts("Warning: "
			     "scrub option will erase all factory set "
			     "bad blocks!\n"
			     "         "
			     "There is no reliable way to recover them.\n"
			     "         "
			     "Use this command only for testing purposes "
			     "if you\n"
			     "         "
			     "are sure of what you are doing!\n"
			     "\nReally scrub this NAND flash? <y/N>\n");

			if (getc() == 'y' && getc() == '\r') {
				opts.scrub = 1;
			} else {
				puts("scrub aborted\n");
				return -1;
			}
		}
		if (NandIsMlc()) {
			printf("off size: %08X %08X\n", off, size);
			if (off % NandBlockSizeInByte != 0 || size % NandBlockSizeInByte != 0) {
				puts("offset and size should be multiple of block size\n");
				ret = -1;
			} else {
				ret = FriendlyARMFormat(off / NandBlockSizeInByte, off / NandBlockSizeInByte + size / NandBlockSizeInByte - 1, scrub);
			}
		} else 
			ret = nand_erase_opts(nand, &opts);
		printf("%s\n", ret ? "ERROR" : "OK");

		return ret == 0 ? 0 : 1;
	}

	if (strncmp(cmd, "dump", 4) == 0) {
		if (argc < 3)
			goto usage;

		s = strchr(cmd, '.');
		off = (int)simple_strtoul(argv[2], NULL, 16);

		if (s != NULL && strcmp(s, ".oob") == 0)
			ret = nand_dump_oob(nand, off);
		else {
			ret = nand_dump_mlc(off);
		}

		return ret == 0 ? 1 : 0;

	}

	/* read write */
	if (strncmp(cmd, "read", 4) == 0 || strncmp(cmd, "write", 5) == 0) {
		int read;

		if (argc < 4)
			goto usage;

		addr = (ulong)simple_strtoul(argv[2], NULL, 16);

		read = strncmp(cmd, "read", 4) == 0; /* 1 = read, 0 = write */
		printf("\nNAND %s: ", read ? "read" : "write");
		if (arg_off_size(argc - 3, argv + 3, nand, &off, &size) != 0)
			return 1;

		s = strchr(cmd, '.');
		if (s != NULL &&
		    (!strcmp(s, ".jffs2") || !strcmp(s, ".e") || !strcmp(s, ".i"))) {
			if (read) {
				ret = FriendlyARMReadNand( (u_char*)addr, size, off);
				if (ret == -1) {
					puts("offset should be multiple of page size\n");
				}
			} else {
				/* write */
				nand_write_options_t opts;
				memset(&opts, 0, sizeof(opts));
				opts.buffer	= (u_char*) addr;
				opts.length	= size;
				opts.offset	= off;
				/* opts.forcejffs2 = 1; */
				opts.pad	= 1;
				opts.blockalign = 1;
				opts.quiet      = quiet;
				if (NandIsMlc()) {
					ret = -1;
					puts("write.jffs2/write.e/write.i is not supported\n");
				} else
					ret = nand_write_opts(nand, &opts);
			}
#ifdef CFG_NAND_YAFFS_WRITE
		} else if (!read && s != NULL && + (!strcmp(s, ".yaffs") || !strcmp(s, ".yaffs1"))) {
			nand_write_options_t opts;
 			memset(&opts, 0, sizeof(opts));
 			opts.buffer = (u_char*) addr;
 			opts.length = size;
 			opts.offset = off;
 			opts.pad = 0;
 			opts.blockalign = 1;
 			opts.quiet = quiet;
 			opts.writeoob = 1;
 			opts.autoplace = 1;

			/* jsgood */
 			/* if (s[6] == '1')
				opts.forceyaffs = 1; */

 			ret = nand_write_opts(nand, &opts);
#endif
 		} else {
			if (read) {
				if (!NandIsMlc()) {
					ret = nand_read(nand, off, &size, (u_char *)addr);
				} else {
					ret = FriendlyARMReadNand( (u_char*)addr, size, off);
					if (ret == -1) {
						puts("offset should be multiple of page size\n");
					}
				}
			} else {
				if (NandIsMlc()) {
					if (off % NandBlockSizeInByte != 0) {
						puts("offset should be multiple of block size\n");
						ret = -1;
					} else {
						unsigned int i;
						ret = 0;
						//add by CHB,if write uboot.bin，use FriendlyARMWriteNandMlcBoot
                        			if(off == 0){
                            				printf("FriendlyARMWriteNandMlcBoot\n");
                            			ret = FriendlyARMWriteNandMlcBoot((u_char *)addr, size, 1);
                        			}else{
							for (i = 0; i < size; i += NandBlockSizeInByte) {
								int len = size - i;
								if (len > NandBlockSizeInByte) {
									len = NandBlockSizeInByte;
								}
								FriendlyARMWriteNand(((u_char *)addr) + i, len, off + i, NandBlockSizeInByte);
							}
						}
					}
				} else {
					ret = nand_write(nand, off, &size, (u_char *)addr);

					if (ret == 0) {
						uint *magic = (uint*)(PHYS_SDRAM_1);
						if ((0x24564236 == magic[0]) && (0x20764316 == magic[1]))
							magic[0] = 0x27051956;
					}
				}
			}
		}

		printf(" %d bytes %s: %s\n", size,
		       read ? "read" : "written", ret ? "ERROR" : "OK");

		return ret == 0 ? 0 : 1;
	}

	if (strcmp(cmd, "markbad") == 0) {
		if (NandIsMlc()) {
			printf("nand markbad is not supported\n");
			return 1;
		}
		addr = (ulong)simple_strtoul(argv[2], NULL, 16);

		int ret = nand->block_markbad(nand, addr);
		if (ret == 0) {
			printf("block 0x%08lx successfully marked as bad\n",
			       (ulong) addr);
			return 0;
		} else {
			printf("block 0x%08lx NOT marked as bad! ERROR %d\n",
			       (ulong) addr, ret);
		}
		return 1;
	}
	if (strcmp(cmd, "biterr") == 0) {
		/* todo */
		return 1;
	}

	if (strcmp(cmd, "lock") == 0) {
		int tight  = 0;
		int status = 0;

		if (NandIsMlc()) {
			printf("nand lock is not supported\n");
			return 1;
		}

		if (argc == 3) {
			if (!strcmp("tight", argv[2]))
				tight = 1;
			if (!strcmp("status", argv[2]))
				status = 1;
		}

		if (status) {
			ulong block_start = 0;
			ulong off;
			int last_status = -1;

			struct nand_chip *nand_chip = nand->priv;
			/* check the WP bit */
			nand_chip->cmdfunc (nand, NAND_CMD_STATUS, -1, -1);
			printf("device is %swrite protected\n",
			       (nand_chip->read_byte(nand) & 0x80 ?
				"NOT " : "" ) );

			for (off = 0; off < nand->size; off += nand->writesize) {
				int s = nand_get_lock_status(nand, off);

				/* print message only if status has changed
				 * or at end of chip
				 */
				if (off == nand->size - nand->writesize
				    || (s != last_status && off != 0))	{

					printf("%08x - %08x: %8d pages %s%s%s\n",
					       block_start,
					       off-1,
					       (off-block_start)/nand->writesize,
					       ((last_status & NAND_LOCK_STATUS_TIGHT) ? "TIGHT " : ""),
					       ((last_status & NAND_LOCK_STATUS_LOCK) ? "LOCK " : ""),
					       ((last_status & NAND_LOCK_STATUS_UNLOCK) ? "UNLOCK " : ""));
				}

				last_status = s;
		       }
		} else {
			if (!nand_lock(nand, tight)) {
				puts("NAND flash successfully locked\n");
			} else {
				puts("Error locking NAND flash\n");
				return 1;
			}
		}
		return 0;
	}

	if (strcmp(cmd, "unlock") == 0) {
		if (arg_off_size(argc - 2, argv + 2, nand, &off, &size) < 0)
			return 1;

		if (!nand_unlock(nand, off, size)) {
			puts("NAND flash successfully unlocked\n");
		} else {
			puts("Error unlocking NAND flash, "
			     "write and erase will probably fail\n");
			return 1;
		}
		return 0;
	}

usage:
	printf("Usage:\n%s\n", cmdtp->usage);
	return 1;
}

U_BOOT_CMD(nand, 5, 1, do_nand,
	"nand    - NAND sub-system\n",
	"info             - show available NAND devices\n"
	"nand device [dev]     - show or set current device\n"
	"nand read[.jffs2]     - addr off|partition size\n"
	"nand write[.jffs2]    - addr off|partiton size - read/write `size' bytes starting\n"
	"    at offset `off' to/from memory address `addr'\n"
#ifdef CFG_NAND_YAFFS_WRITE
	"nand write[.yaffs[1]] - addr off|partition size - write `size' byte yaffs image\n"
	"    starting at offset `off' from memory address `addr' (.yaffs1 for 512+16 NAND)\n"
#endif
 	"nand erase [clean] [off size] - erase `size' bytes from\n"
	"    offset `off' (entire device if not specified)\n"
	"nand bad - show bad blocks\n"
	"nand dump[.oob] off - dump page\n"
	"nand scrub - really clean NAND erasing bad blocks (UNSAFE)\n"
	"nand markbad off - mark bad block at offset (UNSAFE)\n"
	"nand biterr off - make a bit error at offset (UNSAFE)\n"
	"nand lock [tight] [status] - bring nand to lock state or display locked pages\n"
	"nand unlock [offset] [size] - unlock section\n");

static int nand_load_image(cmd_tbl_t *cmdtp, nand_info_t *nand,
			   ulong offset, ulong addr, char *cmd)
{
	int r;
	char *ep;
	ulong cnt;
	image_header_t *hdr;

	printf("\nLoading from %s, offset 0x%lx\n", nand->name, offset);

	cnt = nand->writesize;
	r = nand_read(nand, offset, &cnt, (u_char *) addr);
	if (r) {
		puts("** Read error\n");
		SHOW_BOOT_PROGRESS(-1);
		return 1;
	}

	hdr = (image_header_t *) addr;

	if (ntohl(hdr->ih_magic) != IH_MAGIC) {
		printf("\n** Bad Magic Number 0x%x **\n", hdr->ih_magic);
		SHOW_BOOT_PROGRESS(-1);
		return 1;
	}

	print_image_hdr(hdr);

	cnt = (ntohl(hdr->ih_size) + sizeof (image_header_t));

	r = nand_read(nand, offset, &cnt, (u_char *) addr);
	if (r) {
		puts("** Read error\n");
		SHOW_BOOT_PROGRESS(-1);
		return 1;
	}

	/* Loading ok, update default load address */

	load_addr = addr;

	/* Check if we should attempt an auto-start */
	if (((ep = getenv("autostart")) != NULL) && (strcmp(ep, "yes") == 0)) {
		char *local_args[2];
		extern int do_bootm(cmd_tbl_t *, int, int, char *[]);

		local_args[0] = cmd;
		local_args[1] = NULL;

		printf("Automatic boot of image at addr 0x%08lx ...\n", addr);

		do_bootm(cmdtp, 0, 1, local_args);
		return 1;
	}
	return 0;
}

int do_nandboot(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	char *boot_device = NULL;
	int idx;
	ulong addr, offset = 0;
#if (CONFIG_COMMANDS & CFG_CMD_JFFS2) && defined(CONFIG_JFFS2_CMDLINE)
	struct mtd_device *dev;
	struct part_info *part;
	u8 pnum;

	if (argc >= 2) {
		char *p = (argc == 2) ? argv[1] : argv[2];
		if (!(str2long(p, &addr)) && (mtdparts_init() == 0) &&
		    (find_dev_and_part(p, &dev, &pnum, &part) == 0)) {
			if (dev->id->type != MTD_DEV_TYPE_NAND) {
				puts("Not a NAND device\n");
				return 1;
			}
			if (argc > 3)
				goto usage;
			if (argc == 3)
				addr = simple_strtoul(argv[2], NULL, 16);
			else
				addr = CFG_LOAD_ADDR;
			return nand_load_image(cmdtp, &nand_info[dev->id->num],
					       part->offset, addr, argv[0]);
		}
	}
#endif

	switch (argc) {
	case 1:
		addr = CFG_LOAD_ADDR;
		boot_device = getenv("bootdevice");
		break;
	case 2:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = getenv("bootdevice");
		break;
	case 3:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		break;
	case 4:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		offset = simple_strtoul(argv[3], NULL, 16);
		break;
	default:
#if (CONFIG_COMMANDS & CFG_CMD_JFFS2) && defined(CONFIG_JFFS2_CMDLINE)
usage:
#endif
		printf("Usage:\n%s\n", cmdtp->usage);
		SHOW_BOOT_PROGRESS(-1);
		return 1;
	}

	if (!boot_device) {
		puts("\n** No boot device **\n");
		SHOW_BOOT_PROGRESS(-1);
		return 1;
	}

	idx = simple_strtoul(boot_device, NULL, 16);

	if (idx < 0 || idx >= CFG_MAX_NAND_DEVICE || !nand_info[idx].name) {
		printf("\n** Device %d not available\n", idx);
		SHOW_BOOT_PROGRESS(-1);
		return 1;
	}

	return nand_load_image(cmdtp, &nand_info[idx], offset, addr, argv[0]);
}

U_BOOT_CMD(nboot, 4, 1, do_nandboot,
	"nboot   - boot from NAND device\n",
	"[partition] | [[[loadAddr] dev] offset]\n");

#endif				/* (CONFIG_COMMANDS & CFG_CMD_NAND) */

#else /* CFG_NAND_LEGACY */
/*
 *
 * Legacy NAND support - to be phased out
 *
 */
#include <command.h>
#include <malloc.h>
#include <asm/io.h>
#include <watchdog.h>

#ifdef CONFIG_SHOW_BOOT_PROGRESS
# include <status_led.h>
# define SHOW_BOOT_PROGRESS(arg)	show_boot_progress(arg)
#else
# define SHOW_BOOT_PROGRESS(arg)
#endif

#if (CONFIG_COMMANDS & CFG_CMD_NAND)
#include <linux/mtd/nand_legacy.h>
#if 0
#include <linux/mtd/nand_ids.h>
#include <jffs2/jffs2.h>
#endif

#ifdef CONFIG_OMAP1510
void archflashwp(void *archdata, int wp);
#endif

#define ROUND_DOWN(value,boundary)      ((value) & (~((boundary)-1)))

#undef	NAND_DEBUG
#undef	PSYCHO_DEBUG

/* ****************** WARNING *********************
 * When ALLOW_ERASE_BAD_DEBUG is non-zero the erase command will
 * erase (or at least attempt to erase) blocks that are marked
 * bad. This can be very handy if you are _sure_ that the block
 * is OK, say because you marked a good block bad to test bad
 * block handling and you are done testing, or if you have
 * accidentally marked blocks bad.
 *
 * Erasing factory marked bad blocks is a _bad_ idea. If the
 * erase succeeds there is no reliable way to find them again,
 * and attempting to program or erase bad blocks can affect
 * the data in _other_ (good) blocks.
 */
#define	 ALLOW_ERASE_BAD_DEBUG 0

#define CONFIG_MTD_NAND_ECC  /* enable ECC */
#define CONFIG_MTD_NAND_ECC_JFFS2

/* bits for nand_legacy_rw() `cmd'; or together as needed */
#define NANDRW_READ	0x01
#define NANDRW_WRITE	0x00
#define NANDRW_JFFS2	0x02
#define NANDRW_JFFS2_SKIP	0x04

/*
 * Imports from nand_legacy.c
 */
extern struct nand_chip nand_dev_desc[CFG_MAX_NAND_DEVICE];
extern int curr_device;
extern int nand_legacy_erase(struct nand_chip *nand, size_t ofs,
			    size_t len, int clean);
extern int nand_legacy_rw(struct nand_chip *nand, int cmd, size_t start,
			 size_t len, size_t *retlen, u_char *buf);
extern void nand_print(struct nand_chip *nand);
extern void nand_print_bad(struct nand_chip *nand);
extern int nand_read_oob(struct nand_chip *nand, size_t ofs,
			       size_t len, size_t *retlen, u_char *buf);
extern int nand_write_oob(struct nand_chip *nand, size_t ofs,
				size_t len, size_t *retlen, const u_char *buf);


int do_nand (cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	int rcode = 0;

	switch (argc) {
	case 0:
	case 1:
		printf ("Usage:\n%s\n", cmdtp->usage);
		return 1;
	case 2:
		if (strcmp (argv[1], "info") == 0) {
			int i;

			putc ('\n');

			for (i = 0; i < CFG_MAX_NAND_DEVICE; ++i) {
				if (nand_dev_desc[i].ChipID ==
				    NAND_ChipID_UNKNOWN)
					continue;	/* list only known devices */
				printf ("Device %d: ", i);
				nand_print (&nand_dev_desc[i]);
			}
			return 0;

		} else if (strcmp (argv[1], "device") == 0) {
			if ((curr_device < 0)
			    || (curr_device >= CFG_MAX_NAND_DEVICE)) {
				puts ("\nno devices available\n");
				return 1;
			}
			printf ("\nDevice %d: ", curr_device);
			nand_print (&nand_dev_desc[curr_device]);
			return 0;

		} else if (strcmp (argv[1], "bad") == 0) {
			if ((curr_device < 0)
			    || (curr_device >= CFG_MAX_NAND_DEVICE)) {
				puts ("\nno devices available\n");
				return 1;
			}
			printf ("\nDevice %d bad blocks:\n", curr_device);
			nand_print_bad (&nand_dev_desc[curr_device]);
			return 0;

		}
		printf ("Usage:\n%s\n", cmdtp->usage);
		return 1;
	case 3:
		if (strcmp (argv[1], "device") == 0) {
			int dev = (int) simple_strtoul (argv[2], NULL, 10);

			printf ("\nDevice %d: ", dev);
			if (dev >= CFG_MAX_NAND_DEVICE) {
				puts ("unknown device\n");
				return 1;
			}
			nand_print (&nand_dev_desc[dev]);
			/*nand_print (dev); */

			if (nand_dev_desc[dev].ChipID == NAND_ChipID_UNKNOWN) {
				return 1;
			}

			curr_device = dev;

			puts ("... is now current device\n");

			return 0;
		} else if (strcmp (argv[1], "erase") == 0
			   && strcmp (argv[2], "clean") == 0) {
			struct nand_chip *nand = &nand_dev_desc[curr_device];
			ulong off = 0;
			ulong size = nand->totlen;
			int ret;

			printf ("\nNAND erase: device %d offset %ld, size %ld ... ", curr_device, off, size);

			ret = nand_legacy_erase (nand, off, size, 1);

			printf ("%s\n", ret ? "ERROR" : "OK");

			return ret;
		}

		printf ("Usage:\n%s\n", cmdtp->usage);
		return 1;
	default:
		/* at least 4 args */

		if (strncmp (argv[1], "read", 4) == 0 ||
		    strncmp (argv[1], "write", 5) == 0) {
			ulong addr = simple_strtoul (argv[2], NULL, 16);
			ulong off = simple_strtoul (argv[3], NULL, 16);
			ulong size = simple_strtoul (argv[4], NULL, 16);
			int cmd = (strncmp (argv[1], "read", 4) == 0) ?
				NANDRW_READ : NANDRW_WRITE;
			int ret, total;
			char *cmdtail = strchr (argv[1], '.');

			if (cmdtail && !strncmp (cmdtail, ".oob", 2)) {
				/* read out-of-band data */
				if (cmd & NANDRW_READ) {
					ret = nand_read_oob (nand_dev_desc + curr_device,
							     off, size, (size_t *) & total,
							     (u_char *) addr);
				} else {
					ret = nand_write_oob (nand_dev_desc + curr_device,
							      off, size, (size_t *) & total,
							      (u_char *) addr);
				}
				return ret;
			} else if (cmdtail && !strncmp (cmdtail, ".jffs2", 2))
				cmd |= NANDRW_JFFS2;	/* skip bad blocks */
			else if (cmdtail && !strncmp (cmdtail, ".jffs2s", 2)) {
				cmd |= NANDRW_JFFS2;	/* skip bad blocks (on read too) */
				if (cmd & NANDRW_READ)
					cmd |= NANDRW_JFFS2_SKIP;	/* skip bad blocks (on read too) */
			}
#ifdef SXNI855T
			/* need ".e" same as ".j" for compatibility with older units */
			else if (cmdtail && !strcmp (cmdtail, ".e"))
				cmd |= NANDRW_JFFS2;	/* skip bad blocks */
#endif
#ifdef CFG_NAND_SKIP_BAD_DOT_I
			/* need ".i" same as ".jffs2s" for compatibility with older units (esd) */
			/* ".i" for image -> read skips bad block (no 0xff) */
			else if (cmdtail && !strcmp (cmdtail, ".i")) {
				cmd |= NANDRW_JFFS2;	/* skip bad blocks (on read too) */
				if (cmd & NANDRW_READ)
					cmd |= NANDRW_JFFS2_SKIP;	/* skip bad blocks (on read too) */
			}
#endif /* CFG_NAND_SKIP_BAD_DOT_I */
			else if (cmdtail) {
				printf ("Usage:\n%s\n", cmdtp->usage);
				return 1;
			}

			printf ("\nNAND %s: device %d offset %ld, size %ld ...\n",
				(cmd & NANDRW_READ) ? "read" : "write",
				curr_device, off, size);

			ret = nand_legacy_rw (nand_dev_desc + curr_device,
					      cmd, off, size,
					      (size_t *) & total,
					      (u_char *) addr);

			printf (" %d bytes %s: %s\n", total,
				(cmd & NANDRW_READ) ? "read" : "written",
				ret ? "ERROR" : "OK");

			return ret;
		} else if (strcmp (argv[1], "erase") == 0 &&
			   (argc == 4 || strcmp ("clean", argv[2]) == 0)) {
			int clean = argc == 5;
			ulong off =
				simple_strtoul (argv[2 + clean], NULL, 16);
			ulong size =
				simple_strtoul (argv[3 + clean], NULL, 16);
			int ret;

			printf ("\nNAND erase: device %d offset %ld, size %ld ...\n",
				curr_device, off, size);

			ret = nand_legacy_erase (nand_dev_desc + curr_device,
						 off, size, clean);

			printf ("%s\n", ret ? "ERROR" : "OK");

			return ret;
		} else {
			printf ("Usage:\n%s\n", cmdtp->usage);
			rcode = 1;
		}

		return rcode;
	}
}

U_BOOT_CMD(
	nand,	5,	1,	do_nand,
	"nand    - legacy NAND sub-system\n",
	"info  - show available NAND devices\n"
	"nand device [dev] - show or set current device\n"
	"nand read[.jffs2[s]]  addr off size\n"
	"nand write[.jffs2] addr off size - read/write `size' bytes starting\n"
	"    at offset `off' to/from memory address `addr'\n"
	"nand erase [clean] [off size] - erase `size' bytes from\n"
	"    offset `off' (entire device if not specified)\n"
	"nand bad - show bad blocks\n"
	"nand read.oob addr off size - read out-of-band data\n"
	"nand write.oob addr off size - read out-of-band data\n"
);

int do_nandboot (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	char *boot_device = NULL;
	char *ep;
	int dev;
	ulong cnt;
	ulong addr;
	ulong offset = 0;
	image_header_t *hdr;
	int rcode = 0;
	switch (argc) {
	case 1:
		addr = CFG_LOAD_ADDR;
		boot_device = getenv ("bootdevice");
		break;
	case 2:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = getenv ("bootdevice");
		break;
	case 3:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		break;
	case 4:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		offset = simple_strtoul(argv[3], NULL, 16);
		break;
	default:
		printf ("Usage:\n%s\n", cmdtp->usage);
		SHOW_BOOT_PROGRESS (-1);
		return 1;
	}

	if (!boot_device) {
		puts ("\n** No boot device **\n");
		SHOW_BOOT_PROGRESS (-1);
		return 1;
	}

	dev = simple_strtoul(boot_device, &ep, 16);

	if ((dev >= CFG_MAX_NAND_DEVICE) ||
	    (nand_dev_desc[dev].ChipID == NAND_ChipID_UNKNOWN)) {
		printf ("\n** Device %d not available\n", dev);
		SHOW_BOOT_PROGRESS (-1);
		return 1;
	}

	printf ("\nLoading from device %d: %s at 0x%lx (offset 0x%lx)\n",
		dev, nand_dev_desc[dev].name, nand_dev_desc[dev].IO_ADDR,
		offset);

	if (nand_legacy_rw (nand_dev_desc + dev, NANDRW_READ, offset,
			SECTORSIZE, NULL, (u_char *)addr)) {
		printf ("** Read error on %d\n", dev);
		SHOW_BOOT_PROGRESS (-1);
		return 1;
	}

	hdr = (image_header_t *)addr;

	if (ntohl(hdr->ih_magic) == IH_MAGIC) {

		print_image_hdr (hdr);

		cnt = (ntohl(hdr->ih_size) + sizeof(image_header_t));
		cnt -= SECTORSIZE;
	} else {
		printf ("\n** Bad Magic Number 0x%x **\n", ntohl(hdr->ih_magic));
		SHOW_BOOT_PROGRESS (-1);
		return 1;
	}

	if (nand_legacy_rw (nand_dev_desc + dev, NANDRW_READ,
			offset + SECTORSIZE, cnt, NULL,
			(u_char *)(addr+SECTORSIZE))) {
		printf ("** Read error on %d\n", dev);
		SHOW_BOOT_PROGRESS (-1);
		return 1;
	}

	/* Loading ok, update default load address */

	load_addr = addr;

	/* Check if we should attempt an auto-start */
	if (((ep = getenv("autostart")) != NULL) && (strcmp(ep,"yes") == 0)) {
		char *local_args[2];
		extern int do_bootm (cmd_tbl_t *, int, int, char *[]);

		local_args[0] = argv[0];
		local_args[1] = NULL;

		printf ("Automatic boot of image at addr 0x%08lx ...\n", addr);

		do_bootm (cmdtp, 0, 1, local_args);
		rcode = 1;
	}
	return rcode;
}

U_BOOT_CMD(
	nboot,	4,	1,	do_nandboot,
	"nboot   - boot from NAND device\n",
	"loadAddr dev\n"
);

#endif /* (CONFIG_COMMANDS & CFG_CMD_NAND) */

#endif /* CFG_NAND_LEGACY */
