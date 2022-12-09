#include <common.h>
#include <console.h>
#include <bootretry.h>
#include <cli.h>
#include <command.h>
#include <console.h>
#include <mapmem.h>
#include <watchdog.h>
#include <fs.h>
#include <u-boot/md5.h>
#include <blk.h>
#include <mmc.h>
#include <tmr.h>

static void load_blobs_with_hashes(
	ulong file1, ulong hash1,
	ulong file2, ulong hash2,
	ulong file3, ulong hash3,
	loff_t filesize, ulong *locations
)
{
	struct mmc *mmc;
	printf("Finding mmc device 0\n");
	mmc = find_mmc_device(0);
	mmc->has_init = 0;
	printf("Initializing mmc device 0\n");
	mmc_init(mmc);

	u32 cnt = filesize / YOCTO_BLOCK_SIZE + 1;

	void *addr = (void *)locations[0];
	printf("Reading from mmc\n");
	ulong n = blk_dread(mmc_get_blk_desc(mmc), file1, cnt, addr);
	printf("%lu blocks read: %s\n", n, (n == cnt) ? "OK" : "ERROR");

	addr = (void *)locations[1];
	n = blk_dread(mmc_get_blk_desc(mmc), hash1, 1, addr);
	printf("%lu blocks read: %s\n", n, (n == 1) ? "OK" : "ERROR");

	addr = (void *)locations[2];
	n = blk_dread(mmc_get_blk_desc(mmc), file2, cnt, addr);
	printf("%lu blocks read: %s\n", n, (n == cnt) ? "OK" : "ERROR");

	addr = (void *)locations[3];
	n = blk_dread(mmc_get_blk_desc(mmc), hash2, 1, addr);
	printf("%lu blocks read: %s\n", n, (n == 1) ? "OK" : "ERROR");

	addr = (void *)locations[4];
	n = blk_dread(mmc_get_blk_desc(mmc), file3, cnt, addr);
	printf("%lu blocks read: %s\n", n, (n == cnt) ? "OK" : "ERROR");

	addr = (void *)locations[5];
	n = blk_dread(mmc_get_blk_desc(mmc), hash3, 1, addr);
	printf("%lu blocks read: %s\n", n, (n == 1) ? "OK" : "ERROR");
}

int check_hash(int file, loff_t filesize, ulong *locations) {
	unsigned char calculated_sum[16], tmp[2];

	void *buf = map_sysmem(locations[file*2], filesize);
	md5_wd(buf, filesize, calculated_sum, 64 << 10);
	unmap_sysmem(buf);

	buf = map_sysmem(locations[file*2+1], 32);

	for (int i = 0; i < 16; i++) {
		memcpy(tmp, (u8 *)buf + 2*i, 2);
		if(simple_strtoul(tmp, NULL, 16) != calculated_sum[i]) {
			unmap_sysmem(buf);
			return 0;
		}
	}

	unmap_sysmem(buf);
	return 1;
}

static int majority_wd(ulong addr1, ulong addr2, ulong addr3, ulong dest, ulong bytes) {

	ulong	nread;
	void *buf1, *buf2, *buf3, *buf4;

	ulong word1, word2, word3, word4;

	buf1 = map_sysmem(addr1, bytes);
	buf2 = map_sysmem(addr2, bytes);
	buf3 = map_sysmem(addr3, bytes);
	buf4 = map_sysmem(dest, bytes);

	for (nread = 0; nread < bytes; nread ++) {

		word1 = *(u8 *)buf1;
		word2 = *(u8 *)buf2;
		word3 = *(u8 *)buf3;

		word4 = (word1 & word2) | (word2 & word3) | (word3 & word1); //majority voting gate

		*(u8 *)buf4 = word4;

		buf1 += 1;
		buf2 += 1;
		buf3 += 1;
		buf4 += 1;

		/* reset watchdog from time to time */
		if ((nread % (64 << 10)) == 0)
			WATCHDOG_RESET();

	}

	unmap_sysmem(buf1);
	unmap_sysmem(buf2);
	unmap_sysmem(buf3);
	unmap_sysmem(buf4);

	return 0;

}

static void write_output(ulong input, ulong output, loff_t filesize) {
	memcpy((void *)output, (void *)input, filesize);
}

static bool tmr_blob(
	ulong file1, ulong hash1,
	ulong file2, ulong hash2,
	ulong file3, ulong hash3,
	ulong bytes, ulong output)
{
	loff_t filesize = bytes;
	const ulong first_copy_loc = 0x85000000; // base location to start at for tmr, in u-boot memory
	const ulong first_hash_loc = first_copy_loc + YOCTO_MAX_FILE_BLOCKS * YOCTO_BLOCK_SIZE;
	const ulong second_copy_loc = first_hash_loc + 1024;
	const ulong second_hash_loc = second_copy_loc + YOCTO_MAX_FILE_BLOCKS * YOCTO_BLOCK_SIZE;
	const ulong third_copy_loc = second_hash_loc + 1024;
	const ulong third_hash_loc = third_copy_loc + YOCTO_MAX_FILE_BLOCKS * YOCTO_BLOCK_SIZE;
	const ulong output_loc = third_hash_loc + 1024;
	ulong locations[7] = {first_copy_loc, first_hash_loc, second_copy_loc, second_hash_loc, third_copy_loc, third_hash_loc, output_loc};

	load_blobs_with_hashes(file1, hash1, file2, hash2, file3, hash3, filesize, locations);
	printf("loaded files with hashes\n");
	if (check_hash(0, filesize, locations)) {
		printf("hash for original file was correct\n");
		write_output(locations[0], output, filesize);
		return true;
	} else if (check_hash(1, filesize, locations)) {
		fs_set_blk_dev("mmc", "0:1", FS_TYPE_EXT);
		printf("hash for first backup was correct\n");
		write_output(locations[2], output, filesize);
		return false;
	} else if (check_hash(2, filesize, locations)) {
		fs_set_blk_dev("mmc", "0:1", FS_TYPE_EXT);
		printf("hash for second backup was correct\n");
		write_output(locations[4], output, filesize);
		return false;
	}

	printf("none of the hashes were correct. storing tmr result at 0xa0000000\n");

	majority_wd(locations[0], locations[2], locations[4], locations[6], filesize);
	write_output(locations[6], output, filesize);
	return false;
}