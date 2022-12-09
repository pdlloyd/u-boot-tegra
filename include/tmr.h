#ifndef __TMR_H__
#define __TMR_H__

static void load_blobs_with_hashes(
	ulong file1, ulong hash1,
	ulong file2, ulong hash2,
	ulong file3, ulong hash3,
	loff_t filesize, ulong *locations
);

int check_hash(int file, loff_t filesize, ulong *locations);

static int majority_wd(ulong addr1, ulong addr2, ulong addr3, ulong dest, ulong bytes);

static void write_output(ulong input, ulong output, loff_t filesize);

static bool tmr_blob(
	ulong file1, ulong hash1,
	ulong file2, ulong hash2,
	ulong file3, ulong hash3,
	ulong bytes, ulong output);

#endif	/* __TMR_H__ */
