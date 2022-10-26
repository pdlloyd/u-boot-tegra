// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

/* #define	DEBUG	*/

#include <common.h>
#include <autoboot.h>
#include <bootstage.h>
#include <cli.h>
#include <command.h>
#include <console.h>
#include <env.h>
#include <init.h>
#include <net.h>
#include <version_string.h>
#include <efi_loader.h>
#include <../common/tmr.c>

static void run_preboot_environment_command(void)
{
	char *p;

	p = env_get("preboot");
	if (p != NULL) {
		int prev = 0;

		if (IS_ENABLED(CONFIG_AUTOBOOT_KEYED))
			prev = disable_ctrlc(1); /* disable Ctrl-C checking */

		run_command_list(p, -1, 0);

		if (IS_ENABLED(CONFIG_AUTOBOOT_KEYED))
			disable_ctrlc(prev);	/* restore Ctrl-C checking */
	}
}

/* We come here after U-Boot is initialised and ready to process commands */
void main_loop(void)
{
	const char *s;

	bootstage_mark_name(BOOTSTAGE_ID_MAIN_LOOP, "main_loop");

	if (IS_ENABLED(CONFIG_VERSION_VARIABLE))
		env_set("ver", version_string);  /* set version variable */

	cli_init();

	if (IS_ENABLED(CONFIG_USE_PREBOOT))
		run_preboot_environment_command();

	if (IS_ENABLED(CONFIG_UPDATE_TFTP))
		update_tftp(0UL, NULL, NULL);

	if (IS_ENABLED(CONFIG_EFI_CAPSULE_ON_DISK_EARLY))
		efi_launch_capsules();

	s = bootdelay_process();
	if (cli_process_fdt(&s))
		cli_secure_boot_cmd(s);

	autoboot_command(s);

	printf("meta-sol ref YOCTO_SOL_REF\n");
	fs_set_blk_dev("mmc", "0:1", FS_TYPE_EXT);

	ulong file_offset[8] = {YOCTO_INFO_FILE_OFFSET, YOCTO_INFO_HASH_OFFSET,
                            YOCTO_IMAGE_FILE_OFFSET, YOCTO_IMAGE_HASH_OFFSET,
                            YOCTO_DTB_FILE_OFFSET, YOCTO_DTB_HASH_OFFSET,
                            YOCTO_INITRD_FILE_OFFSET, YOCTO_INITRD_HASH_OFFSET};
    ulong part_size = YOCTO_ROOTFSPART_SIZE / YOCTO_BLOCK_SIZE;
    ulong part_offset[3] = {YOCTO_PARTITION_OFFSET,
                            YOCTO_PARTITION_OFFSET + part_size,
                            YOCTO_PARTITION_OFFSET + (2 * part_size)};
    ulong outputs[4];
    outputs[0] = 0xa5000000; // in u-boot memory
    outputs[1] = outputs[0] + YOCTO_INFO_FILE_BLOCKS * YOCTO_BLOCK_SIZE;
    outputs[2] = outputs[1] + YOCTO_IMAGE_FILE_BLOCKS * YOCTO_BLOCK_SIZE;
    outputs[3] = outputs[2] + YOCTO_DTB_FILE_BLOCKS * YOCTO_BLOCK_SIZE;
    ulong sizes[4];
    sizes[0] = YOCTO_INFO_BYTES * 4; //info file, 4 filesizes
    char *safe = "s=----";

    printf("TMRing info files\n");
    if(
        tmr_blob(
            part_offset[0] + file_offset[0],
            part_offset[0] + file_offset[1],
            part_offset[1] + file_offset[0],
            part_offset[1] + file_offset[1],
            part_offset[2] + file_offset[0],
            part_offset[2] + file_offset[1],
            sizes[0], outputs[0])
    ) {
        safe[2] = 'x';
    }

    printf("Reading info for filesizes\n");
    char tmp[YOCTO_INFO_BYTES];
    char *info = (char *)map_sysmem(outputs[0], YOCTO_INFO_BYTES*3);
    for (int i = 0; i < 3; i ++) {
        memcpy(tmp, info + YOCTO_INFO_BYTES * i, YOCTO_INFO_BYTES);
        sizes[i+1] = simple_strtoul(tmp, NULL, 10);
    }
    unmap_sysmem((const void*) info);

    for (int i = 1; i < 4; i ++) {
        printf("TMRing file #%d of size %lu\n", i, sizes[i]);
        if(
            !tmr_blob(
                part_offset[0] + file_offset[2*i],
                part_offset[0] + file_offset[2*i+1],
                part_offset[1] + file_offset[2*i],
                part_offset[1] + file_offset[2*i+1],
                part_offset[2] + file_offset[2*i],
                part_offset[2] + file_offset[2*i+1],
                sizes[i], outputs[i])
        ) {
            // pass info to kernel
            safe[i+2] = 'x';
        }
    }

    // TODO: See if we can find a way to make use of bootargs
    //setenv("bootargs", safe);
    char bootargs[CONFIG_SYS_CBSIZE];
    cli_simple_process_macros("${cbootargs} root=/dev/ram0 rw rootwait ${bootargs}", bootargs);
    setenv("bootargs", bootargs);

    if(!abortboot(5)) {
        char initrd_loc[YOCTO_INFO_BYTES*2] = "";
        sprintf(initrd_loc, "%x:%x", outputs[3], sizes[3]);
		char image_loc[YOCTO_INFO_BYTES] = "";
        sprintf(image_loc, "%x", outputs[1]);
        char dtb_loc[YOCTO_INFO_BYTES] = "";
        sprintf(dtb_loc, "%x", outputs[2]);
        char *argv[4] = {"booti", image_loc,
                         initrd_loc,
                         dtb_loc};
        cmd_tbl_t *bcmd = find_cmd("booti");
        do_booti(bcmd, 0, 4, argv);
    } else {
	cli_loop();
	panic("No CLI available");
    }
}
