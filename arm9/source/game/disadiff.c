#include "disadiff.h"
#include "image.h"
#include "vff.h"
#include "sha.h"
#include "ui.h"

#define GET_DPFS_BIT(b, lvl) (((((u32*) (void*) lvl)[b >> 5]) >> (31 - (b % 32))) & 1)


static FIL ddfile;
static FIL* ddfp = NULL;

inline static u32 DisaDiffSize(const TCHAR* path) {
    return path ? fvx_qsize(path) : GetMountSize();
}

inline static FRESULT DisaDiffOpen(const TCHAR* path) {
    FRESULT res = FR_OK;
    
    ddfp = NULL;
    if (path) {
        res = fvx_open(&ddfile, path, FA_READ | FA_WRITE |  FA_OPEN_EXISTING);
        if (res == FR_OK) ddfp = &ddfile;
    } else if (!GetMountState()) res = FR_DENIED;
    
    return res;
}

inline static FRESULT DisaDiffRead(void* buf, UINT btr, UINT ofs) {
    if (ddfp) {
        FRESULT res;
        UINT br;
        if ((fvx_tell(ddfp) != ofs) &&
            (fvx_lseek(ddfp, ofs) != FR_OK)) return FR_DENIED;
        res = fvx_read(ddfp, buf, btr, &br);
        if ((res == FR_OK) && (br != btr)) res = FR_DENIED;
        return res;
    } else return (ReadImageBytes(buf, (u64) ofs, (u64) btr) == 0) ? FR_OK : FR_DENIED;
}

inline static FRESULT DisaDiffWrite(const void* buf, UINT btw, UINT ofs) {
    if (ddfp) {
        FRESULT res;
        UINT bw;
        if ((fvx_tell(ddfp) != ofs) &&
            (fvx_lseek(ddfp, ofs) != FR_OK)) return FR_DENIED;
        res = fvx_write(ddfp, buf, btw, &bw);
		if ((res == FR_OK) && (bw != btw)) res = FR_DENIED;
        return res;
    } else return (WriteImageBytes(buf, (u64) ofs, (u64) btw) == 0) ? FR_OK : FR_DENIED;
}

inline static FRESULT DisaDiffClose() {
    if (ddfp) {
        ddfp = NULL;
        return fvx_close(&ddfile);
    } else return FR_OK;
}

inline static FRESULT DisaDiffQRead(const TCHAR* path, void* buf, UINT ofs, UINT btr) {
    if (path) return fvx_qread(path, buf, ofs, btr, NULL);
    else return (ReadImageBytes(buf, (u64) ofs, (u64) btr) == 0) ? FR_OK : FR_DENIED;
}

inline static FRESULT DisaDiffQWrite(const TCHAR* path, const void* buf, UINT ofs, UINT btw) {
    if (path) return fvx_qwrite(path, buf, ofs, btw, NULL);
    else return (WriteImageBytes(buf, (u64) ofs, (u64) btw) == 0) ? FR_OK : FR_DENIED;
}


u32 GetDisaDiffReaderInfo(const char* path, DisaDiffReaderInfo* info, bool partitionB) {
    const u8 disa_magic[] = { DISA_MAGIC };
    const u8 diff_magic[] = { DIFF_MAGIC };
    const u8 ivfc_magic[] = { IVFC_MAGIC };
    const u8 dpfs_magic[] = { DPFS_MAGIC };
    const u8 difi_magic[] = { DIFI_MAGIC };
    
    // reset reader info
    memset(info, 0x00, sizeof(DisaDiffReaderInfo));
    
    // get file size, header at header offset
    u32 file_size = DisaDiffSize(path);
    u8 header[0x100];
    if (DisaDiffQRead(path, header, 0x100, 0x100) != FR_OK)
	{
		ShowPrompt(false, "Failed to read header for building reader info");
		return 1;
    }
	
    // DISA/DIFF header: find partition offset & size and DIFI descriptor
    u32 offset_partition = 0;
    u32 size_partition = 0;
    u32 offset_difi = 0;
    if (memcmp(header, disa_magic, 8) == 0) { // DISA file
        DisaHeader* disa = (DisaHeader*) (void*) header;
        offset_difi = (disa->active_table) ? disa->offset_table1 : disa->offset_table0;
        if (!partitionB) {
            offset_partition = (u32) disa->offset_partitionA;
            size_partition = (u32) disa->size_partitionA;
            offset_difi += (u32) disa->offset_descA;
        } else {
            if (disa->n_partitions != 2) return 1;
            offset_partition = (u32) disa->offset_partitionB;
            size_partition = (u32) disa->size_partitionB;
            offset_difi += (u32) disa->offset_descB;
        }
		info->offset_partition_hash = 0x16C;
    } else if (memcmp(header, diff_magic, 8) == 0) { // DIFF file
        if (partitionB)
		{
			ShowPrompt(false, "partitionB true when building reader info for a diff file");
			return 1;
		}
        DiffHeader* diff = (DiffHeader*) (void*) header;
        offset_partition = (u32) diff->offset_partition;
        size_partition = (u32) diff->size_partition;
        offset_difi = (diff->active_table) ? diff->offset_table1 : diff->offset_table0;
		info->offset_partition_hash = 0x134;
    } else {
		ShowPrompt(false, "Unable to find DIFF or DISA magic");
		return 1;
	}
    
    // check the output so far
    if (!offset_difi || (offset_difi + sizeof(DifiStruct) > file_size) || (offset_partition + size_partition > file_size))
	{
		ShowPrompt(false, "First ReaderInfo output check failed.\nfile_size was 0x%X\noffset_difi was 0x%X\noffset_partition was 0x%X\nsize_partition was 0x%X",
			file_size, offset_difi, offset_partition, size_partition);
        return 1;
	}
        
    info->offset_difi = offset_difi;
    // read DIFI struct from filr
    DifiStruct difis;
    if (DisaDiffQRead(path, &difis, offset_difi, sizeof(DifiStruct)) != FR_OK)
	{
		ShowPrompt(false, "Failed to read difi struct for building ReaderInfo");
		return 1;
	}
    if ((memcmp(difis.difi.magic, difi_magic, 8) != 0) ||
        (memcmp(difis.ivfc.magic, ivfc_magic, 8) != 0) ||
        (memcmp(difis.dpfs.magic, dpfs_magic, 8) != 0))
	{
		ShowPrompt(false, "difi magic check fail when building ReaderInfo");
        return 1;
	}
    
    // check & get data from DIFI header
    DifiHeader* difi = &(difis.difi);
    if ((difi->offset_ivfc != sizeof(DifiHeader)) ||
        (difi->size_ivfc != sizeof(IvfcDescriptor)) ||
        (difi->offset_dpfs != difi->offset_ivfc + difi->size_ivfc) ||
        (difi->size_dpfs != sizeof(DpfsDescriptor)) ||
        (difi->offset_hash != difi->offset_dpfs + difi->size_dpfs) ||
        (difi->size_hash < 0x20))
	{
		ShowPrompt(false, "difi header data check failed");
        return 1;
	}
    
    info->dpfs_lvl1_selector = difi->dpfs_lvl1_selector;
    info->ivfc_use_extlvl4 = difi->ivfc_use_extlvl4;
    info->offset_ivfc_lvl4 = (u32) (offset_partition + difi->ivfc_offset_extlvl4);
	info->offset_master_hash = (u32) difi->offset_hash;
	info->size_master_hash = (u32) difi->size_hash;
    
    // check & get data from DPFS descriptor
    DpfsDescriptor* dpfs = &(difis.dpfs);
    if ((dpfs->offset_lvl1 + dpfs->size_lvl1 > dpfs->offset_lvl2) ||
        (dpfs->offset_lvl2 + dpfs->size_lvl2 > dpfs->offset_lvl3) ||
        (dpfs->offset_lvl3 + dpfs->size_lvl3 > size_partition) ||
        (2 > dpfs->log_lvl2) || (dpfs->log_lvl2 > dpfs->log_lvl3) ||
        !dpfs->size_lvl1 || !dpfs->size_lvl2 || !dpfs->size_lvl3)
	{
		ShowPrompt(false, "dpfs descriptor data check failed");
        return 1;
	}
    
    info->offset_dpfs_lvl1 = (u32) (offset_partition + dpfs->offset_lvl1);
    info->offset_dpfs_lvl2 = (u32) (offset_partition + dpfs->offset_lvl2);
    info->offset_dpfs_lvl3 = (u32) (offset_partition + dpfs->offset_lvl3);
    info->size_dpfs_lvl1 = (u32) dpfs->size_lvl1;
    info->size_dpfs_lvl2 = (u32) dpfs->size_lvl2;
    info->size_dpfs_lvl3 = (u32) dpfs->size_lvl3;
    info->log_dpfs_lvl2 = (u32) dpfs->log_lvl2;
    info->log_dpfs_lvl3 = (u32) dpfs->log_lvl3;
        
    // check & get data from IVFC descriptor
    IvfcDescriptor* ivfc = &(difis.ivfc);
    if ((ivfc->size_hash != difi->size_hash) ||
        (ivfc->size_ivfc != sizeof(IvfcDescriptor)) ||
        (ivfc->offset_lvl1 + ivfc->size_lvl1 > ivfc->offset_lvl2) ||
        (ivfc->offset_lvl2 + ivfc->size_lvl2 > ivfc->offset_lvl3) ||
        (ivfc->offset_lvl3 + ivfc->size_lvl3 > dpfs->size_lvl3))
	{
		ShowPrompt(false, "ivfc descriptor generic data check failed");
        return 1;
	}
    if (!info->ivfc_use_extlvl4) {
        if ((ivfc->offset_lvl3 + ivfc->size_lvl3 > ivfc->offset_lvl4) ||
            (ivfc->offset_lvl4 + ivfc->size_lvl4 > dpfs->size_lvl3))
		{
			ShowPrompt(false, "Non-external ivfc check failed");
            return 1;
		}
        info->offset_ivfc_lvl4 = (u32) ivfc->offset_lvl4;
    } else if (info->offset_ivfc_lvl4 + ivfc->size_lvl4 > offset_partition + size_partition) {
		ShowPrompt(false, "External ivfc lvl 4 overflowed partition");
        return 1;
    }
	info->log_ivfc_lvl1 = (u32) ivfc->log_lvl1;
	info->log_ivfc_lvl2 = (u32) ivfc->log_lvl2;
	info->log_ivfc_lvl3 = (u32) ivfc->log_lvl3;
	info->log_ivfc_lvl4 = (u32) ivfc->log_lvl4;
	info->offset_ivfc_lvl1 = (u32) ivfc->offset_lvl1;
	info->offset_ivfc_lvl2 = (u32) ivfc->offset_lvl2;
	info->offset_ivfc_lvl3 = (u32) ivfc->offset_lvl3;
	info->size_ivfc_lvl1 = (u32) ivfc->size_lvl1;
	info->size_ivfc_lvl2 = (u32) ivfc->size_lvl2;
	info->size_ivfc_lvl3 = (u32) ivfc->size_lvl3;
    info->size_ivfc_lvl4 = (u32) ivfc->size_lvl4;
    
    return 0;
}

u32 BuildDisaDiffDpfsLvl2Cache(const char* path, DisaDiffReaderInfo* info, u8* cache, u32 cache_size) {
    const u32 min_cache_bits = (info->size_dpfs_lvl3 + (1 << info->log_dpfs_lvl3) - 1) >> info->log_dpfs_lvl3;
    const u32 min_cache_size = ((min_cache_bits + 31) >> (3 + 2)) << 2;
    const u32 offset_lvl1 = info->offset_dpfs_lvl1 + ((info->dpfs_lvl1_selector) ? info->size_dpfs_lvl1 : 0);
    
    // safety (this still assumes all the checks from GetDisaDiffReaderInfo())
    if (info->ivfc_use_extlvl4 ||
        (cache_size < min_cache_size) ||
        (min_cache_size > info->size_dpfs_lvl2) ||
        (min_cache_size > (info->size_dpfs_lvl1 << (3 + info->log_dpfs_lvl2))))
        return 1;
    
    // allocate memory
    u8* lvl1 = (u8*) malloc(info->size_dpfs_lvl1);
    if (!lvl1) return 1; // this is never more than 8 byte in reality -___-
    
    // open file pointer
    if (DisaDiffOpen(path) != FR_OK) {
        free(lvl1);
        return 1;
    }
    
    // read lvl1
    u32 ret = 0;
    if ((ret != 0) || DisaDiffRead(lvl1, info->size_dpfs_lvl1, offset_lvl1)) ret = 1;
    
    // read full lvl2_0 to cache
    if ((ret != 0) || DisaDiffRead(cache, info->size_dpfs_lvl2, info->offset_dpfs_lvl2)) ret = 1;
    
    // cherry-pick lvl2_1
    u32 log_lvl2 = info->log_dpfs_lvl2;
    u32 offset_lvl2_1 = info->offset_dpfs_lvl2 + info->size_dpfs_lvl2;
    for (u32 i = 0; (ret == 0) && ((i << (3 + log_lvl2)) < min_cache_size); i += 4) {
        u32 dword = *(u32*) (void*) (lvl1 + i);
        for (u32 b = 0; b < 32; b++) {
            if ((dword >> (31 - b)) & 1) {
                u32 offset = ((i << 3) + b) << log_lvl2;
                if (DisaDiffRead((u8*) cache + offset, 1 << log_lvl2, offset_lvl2_1 + offset) != FR_OK) ret = 1;
            }
        }
    }
    
    info->dpfs_lvl2_cache = cache;
    free(lvl1);
    DisaDiffClose();
    return ret;
}

static u32 ReadDisaDiffDpfsLvl3(DisaDiffReaderInfo* info, u32 offset, u32 size, void* buffer) { // assumes file is already open
	const u32 offset_start = offset;
	const u32 offset_end = offset_start + size;
	const u8* lvl2 = info->dpfs_lvl2_cache;
	const u32 offset_lvl3_0 = info->offset_dpfs_lvl3;
	const u32 offset_lvl3_1 = offset_lvl3_0 + info->size_dpfs_lvl3;
	const u32 log_lvl3 = info->log_dpfs_lvl3;
	
	u32 read_start = offset_start;
	u32 read_end = read_start;
	u32 bit_state = 0;
	
	// full reading below
	while (size && (read_start < offset_end)) {
		// read bits until bit_state does not match
		// idx_lvl2 is a bit offset
		u32 idx_lvl2 = read_end >> log_lvl3;
		if (GET_DPFS_BIT(idx_lvl2, lvl2) == bit_state) {
			read_end = (idx_lvl2+1) << log_lvl3;
			if (read_end >= offset_end) read_end = offset_end;
			else continue;
		}
		// read data if there is any
		if (read_start < read_end) {
			const u32 pos_f = (bit_state ? offset_lvl3_1 : offset_lvl3_0) + read_start;
			const u32 pos_b = read_start - offset_start;
			const u32 btr = read_end - read_start;
			//ShowPrompt(false, "Reading.\nread_start is 0x%X\nread_end is 0x%X\npos_f is 0x%X\npos_b is 0x%X\nbtr is 0x%X", read_start, read_end, pos_f, pos_b, btr);
			if (DisaDiffRead(((u8*) buffer) + pos_b, btr, pos_f) != FR_OK) size = 0;
			read_start = read_end;
		}
		// flip the bit_state
		bit_state = ~bit_state & 0x1;
	}
	
	return size;
}

static u32 WriteDisaDiffDpfsLvl3(DisaDiffReaderInfo* info, u32 offset, u32 size, const void* buffer) { // assumes file is already open, does not fix hashes
	const u32 offset_start = offset;
	const u32 offset_end = offset_start + size;
	const u8* lvl2 = info->dpfs_lvl2_cache;
	const u32 offset_lvl3_0 = info->offset_dpfs_lvl3;
	const u32 offset_lvl3_1 = offset_lvl3_0 + info->size_dpfs_lvl3;
	const u32 log_lvl3 = info->log_dpfs_lvl3;
	
	u32 write_start = offset_start;
	u32 write_end = write_start;
	u32 bit_state = 0;
	
	// full reading below
	while (size && (write_start < offset_end)) {
		// write bits until bit_state does not match
		// idx_lvl2 is a bit offset
		u32 idx_lvl2 = write_end >> log_lvl3;
		if (GET_DPFS_BIT(idx_lvl2, lvl2) == bit_state) {
			write_end = (idx_lvl2+1) << log_lvl3;
			if (write_end >= offset_end) write_end = offset_end;
			else continue;
		}
		// write data if there is any
		if (write_start < write_end) {
			const u32 pos_f = (bit_state ? offset_lvl3_1 : offset_lvl3_0) + write_start;
			const u32 pos_b = write_start - offset_start;
			const u32 btw = write_end - write_start;
			//ShowPrompt(false, "Writing.\nwrite_start is 0x%X\nwrite_end is 0x%X\npos_f is 0x%X\npos_b is 0x%X\nbtw is 0x%X", write_start, write_end, pos_f, pos_b, btw);
			u32 write_ret = DisaDiffWrite(((u8*) buffer) + pos_b, btw, pos_f);
			if (write_ret != FR_OK)
			{
				ShowPrompt(false, "DisaDiffWrite returned 0x%X\nbtw was 0x%X\npos_f was 0x%X\npos_b was 0x%X", write_ret, btw, pos_f, pos_b);
				size = 0;
			}
			write_start = write_end;
		}
		// flip the bit_state
		bit_state = ~bit_state & 0x1;
	}
	
	return size;
}
	
u32 ReadDisaDiffIvfcLvl4(const char* path, DisaDiffReaderInfo* info, u32 offset, u32 size, void* buffer) {
    // data: full DISA/DIFF file in memory
    // offset: offset inside IVFC lvl4
    
    // DisaDiffReaderInfo not provided?
    DisaDiffReaderInfo info_l;
    u8* cache = NULL;
    if (!info) {
        info = &info_l;
        if (GetDisaDiffReaderInfo(path, info, false) != 0) return 0;
        cache = malloc(info->size_dpfs_lvl2);
        if (!cache) return 0;
        if (BuildDisaDiffDpfsLvl2Cache(path, info, cache, info->size_dpfs_lvl2) != 0) {
            free(cache);
            return 0;
        }
    }
   
    // sanity checks - offset & size
    if (offset > info->size_ivfc_lvl4) return 0;
    else if (offset + size > info->size_ivfc_lvl4) size = info->size_ivfc_lvl4 - offset;
    
    if (info->ivfc_use_extlvl4) {
        // quick reading in case of external IVFC lvl4
        if (DisaDiffQRead(path, buffer, info->offset_ivfc_lvl4 + offset, size) != FR_OK) size = 0;
    } else {
        
        // open file pointer
        if (DisaDiffOpen(path) != FR_OK) size = 0;

        size = ReadDisaDiffDpfsLvl3(info, info->offset_ivfc_lvl4 + offset, size, buffer);

        DisaDiffClose();
    }
    
    if (cache) free(cache);
    return size;
}

u32 FixDisaDiffIvfcHashChain(const char* path, DisaDiffReaderInfo* info, u32 offset, u32 size) { // offset relative to start of ivfc lvl4. cmac still needs fixed after calling this
	// DisaDiffReaderInfo not provided?
    DisaDiffReaderInfo info_l;
    u8* cache = NULL;
    if (!info) {
        info = &info_l;
        if (GetDisaDiffReaderInfo(path, info, false) != 0) return 1;
        cache = malloc(info->size_dpfs_lvl2);
        if (!cache) return 1;
        if (BuildDisaDiffDpfsLvl2Cache(path, info, cache, info->size_dpfs_lvl2) != 0) {
            free(cache);
            return 1;
        }
    }
	
	if (DisaDiffOpen(path) != FR_OK) return 1;
	
	u32 lvl4_offset = (offset >> info->log_ivfc_lvl4) << info->log_ivfc_lvl4; // align starting offset
	u32 lvl4_size = size + offset - lvl4_offset;
	const u32 lvl4_block_size = 1 << info->log_ivfc_lvl4;
	u32 lvl4_read_size = lvl4_block_size;
	u32 lvl3_offset = (((lvl4_offset >> info->log_ivfc_lvl4) * 0x20) >> info->log_ivfc_lvl3) << info->log_ivfc_lvl3;
	u32 lvl3_size = ((lvl4_size >> info->log_ivfc_lvl4) + (((lvl4_size % lvl4_block_size) == 0) ? 0 : 1)) * 0x20;
	const u32 lvl3_block_size = 1 << info->log_ivfc_lvl3;
	u32 lvl3_read_size = lvl3_block_size;
	u32 lvl2_offset = (((lvl3_offset >> info->log_ivfc_lvl3) * 0x20) >> info->log_ivfc_lvl2) << info->log_ivfc_lvl2;
	u32 lvl2_size = ((lvl3_size >> info->log_ivfc_lvl3) + (((lvl3_size % lvl3_block_size) == 0) ? 0 : 1)) * 0x20;
	const u32 lvl2_block_size = 1 << info->log_ivfc_lvl2;
	u32 lvl2_read_size = lvl2_block_size;
	u32 lvl1_offset = (((lvl2_offset >> info->log_ivfc_lvl2) * 0x20) >> info->log_ivfc_lvl1) << info->log_ivfc_lvl1;
	u32 lvl1_size = ((lvl2_size >> info->log_ivfc_lvl2) + (((lvl2_size % lvl2_block_size) == 0) ? 0 : 1)) * 0x20;
	const u32 lvl1_block_size = 1 << info->log_ivfc_lvl1;
	u32 lvl1_read_size = lvl1_block_size;
	
	u8* buf;
	u8 shabuf[0x20];
	
	if (!(buf = malloc(lvl4_block_size)))
		return 1;
	
	while (lvl4_size > 0)
	{
		//ShowPrompt(false, "lvl4 loop.\nlvl4_size is 0x%X\nlvl4_block_size is 0x%X", lvl4_size, lvl4_block_size);
		
		if (lvl4_offset + lvl4_block_size > info->size_ivfc_lvl4)
		{
			memset(buf, 0, lvl4_block_size);
			lvl4_read_size -= (lvl4_offset + lvl4_block_size - info->size_ivfc_lvl4);
		}
		
		if (info->ivfc_use_extlvl4 ? (DisaDiffRead(buf, lvl4_read_size, lvl4_offset + info->offset_ivfc_lvl4) != FR_OK) :
			(ReadDisaDiffDpfsLvl3(info, lvl4_offset + info->offset_ivfc_lvl4, lvl4_read_size, buf) != lvl4_read_size))
		{
			free(buf);			
			return 1;
		}
		
		sha_quick(shabuf, buf, lvl4_block_size, SHA256_MODE);
		
		if (WriteDisaDiffDpfsLvl3(info, info->offset_ivfc_lvl3 + ((lvl4_offset >> info->log_ivfc_lvl4) * 0x20), 0x20, shabuf) != 0x20)
		{
			free(buf);
			return 1;
		}
		
		lvl4_offset += lvl4_block_size;
		lvl4_size = ((lvl4_size < lvl4_block_size) ? 0 : (lvl4_size - lvl4_block_size));
	}
	
	free(buf);
	
	
	if (!(buf = malloc(lvl3_block_size)))
		return 1;
	
	while (lvl3_size > 0)
	{
		//ShowPrompt(false, "lvl3 loop.\nlvl3_size is 0x%X\nlvl3_block_size is 0x%X", lvl3_size, lvl3_block_size);
		
		if (lvl3_offset + lvl3_block_size > info->size_ivfc_lvl3)
		{
			memset(buf, 0, lvl3_block_size);
			lvl3_read_size -= (lvl3_offset + lvl3_block_size - info->size_ivfc_lvl3);
		}
		
		if (ReadDisaDiffDpfsLvl3(info, lvl3_offset + info->offset_ivfc_lvl3, lvl3_read_size, buf) != lvl3_read_size)
		{
			free(buf);
			return 1;
		}
		
		sha_quick(shabuf, buf, lvl3_block_size, SHA256_MODE);
		
		if (WriteDisaDiffDpfsLvl3(info, info->offset_ivfc_lvl2 + ((lvl3_offset >> info->log_ivfc_lvl3) * 0x20), 0x20, shabuf) != 0x20)
		{
			free(buf);
			return 1;
		}
		
		lvl3_offset += lvl3_block_size;
		lvl3_size = ((lvl3_size < lvl3_block_size) ? 0 : (lvl3_size - lvl3_block_size));
	}
	
	free(buf);
	
	
	if (!(buf = malloc(lvl2_block_size)))
		return 1;
	
	while (lvl2_size > 0)
	{
		if (lvl2_offset + lvl2_block_size > info->size_ivfc_lvl2)
		{
			memset(buf, 0, lvl2_block_size);
			lvl2_read_size -= (lvl2_offset + lvl2_block_size - info->size_ivfc_lvl2);
		}
		
		//ShowPrompt(false, "lvl2 loop.\nlvl2_offset is 0x%X\nlvl2_size is 0x%X\nlvl2_block_size is 0x%X\nlvl2_read_size is 0x%X", lvl2_offset, lvl2_size, lvl2_block_size, lvl2_read_size);
		
		if (ReadDisaDiffDpfsLvl3(info, lvl2_offset + info->offset_ivfc_lvl2, lvl2_read_size, buf) != lvl2_read_size)
		{
			free(buf);
			return 1;
		}
		
		sha_quick(shabuf, buf, lvl2_block_size, SHA256_MODE);
		
		if (WriteDisaDiffDpfsLvl3(info, info->offset_ivfc_lvl1 + ((lvl2_offset >> info->log_ivfc_lvl2) * 0x20), 0x20, shabuf) != 0x20)
		{
			free(buf);
			return 1;
		}
		
		lvl2_offset += lvl2_block_size;
		lvl2_size = ((lvl2_size < lvl2_block_size) ? 0 : (lvl2_size - lvl2_block_size));
	}
	
	free(buf);
	
	
	if (!(buf = malloc(lvl1_block_size)))
		return 1;
	
	while (lvl1_size > 0)
	{
		if (lvl1_offset + lvl1_block_size > info->size_ivfc_lvl1)
		{
			memset(buf, 0, lvl1_block_size);
			lvl1_read_size -= (lvl1_offset + lvl1_block_size - info->size_ivfc_lvl1);
		}
		
		//ShowPrompt(false, "lvl1 loop.\nlvl1_offset is 0x%X\nlvl1_size is 0x%X\nlvl1_block_size is 0x%X\nlvl1_read_size is 0x%X", lvl1_offset, lvl1_size, lvl1_block_size, lvl1_read_size);
		
		if (ReadDisaDiffDpfsLvl3(info, lvl1_offset + info->offset_ivfc_lvl1, lvl1_read_size, buf) != lvl1_read_size)
		{
			free(buf);
			return 1;
		}
		
		sha_quick(shabuf, buf, lvl1_block_size, SHA256_MODE);
		
		if (DisaDiffWrite(shabuf, 0x20, info->offset_difi + info->offset_master_hash + ((lvl1_offset >> info->log_ivfc_lvl1) * 0x20)) != FR_OK)
		{
			free(buf);
			return 1;
		}
		
		lvl1_offset += lvl1_block_size;
		lvl1_size = ((lvl1_size < lvl1_block_size) ? 0 : (lvl1_size - lvl1_block_size));
	}
	
	free(buf);
	
	
	if (!(buf = malloc(info->offset_master_hash + info->size_master_hash)))
		return 1;
	
	if (DisaDiffRead(buf, info->offset_master_hash + info->size_master_hash, info->offset_difi) != FR_OK)
	{
		free(buf);
		return 1;
	}
	
	sha_quick(shabuf, buf, info->offset_master_hash + info->size_master_hash, SHA256_MODE);
	
	free(buf);
	
	if (DisaDiffWrite(shabuf, 0x20, info->offset_partition_hash) != FR_OK)
		return 1;
	
	DisaDiffClose();
	
	return 0;
}

u32 WriteDisaDiffIvfcLvl4(const char* path, DisaDiffReaderInfo* info, u32 offset, u32 size, const void* buffer) {
	// offset: offset inside IVFC lvl4
    
    // DisaDiffReaderInfo not provided?
    DisaDiffReaderInfo info_l;
    u8* cache = NULL;
    if (!info) {
        info = &info_l;
        if (GetDisaDiffReaderInfo(path, info, false) != 0)
		{
			ShowPrompt(false, "Failed to build ReaderInfo");
			return 0;
		}
        cache = malloc(info->size_dpfs_lvl2);
        if (!cache) return 0;
        if (BuildDisaDiffDpfsLvl2Cache(path, info, cache, info->size_dpfs_lvl2) != 0) {
			ShowPrompt(false, "Failed to build dpfs lvl 2 cache");
            free(cache);
            return 0;
        }
    }
   
    // sanity check - offset & size
    if (offset + size > info->size_ivfc_lvl4)
	{
		ShowPrompt(false, "Tried to write past end of ivfc lvl 4");
		return 0;
	}
    
    if (info->ivfc_use_extlvl4) {
        // quick writing in case of external IVFC lvl4
        if (DisaDiffQWrite(path, buffer, info->offset_ivfc_lvl4 + offset, size) != FR_OK)
		{
			ShowPrompt(false, "QWrite failed with external ivfc lvl 4");
			size = 0;
		}
    } else {
        
        // open file pointer
        if (DisaDiffOpen(path) != FR_OK)
		{
			ShowPrompt(false, "Open failed with standard write");
			size = 0;
		}

        if ((size = WriteDisaDiffDpfsLvl3(info, info->offset_ivfc_lvl4 + offset, size, buffer)) == 0)
			ShowPrompt(false, "lvl 3 write failed in standard write\noffset in dpfs lvl 3 was 0x%X", info->offset_ivfc_lvl4 + offset);

        DisaDiffClose();
    }
    
	if ((size != 0) && (FixDisaDiffIvfcHashChain(path, info, offset, size) != 0))
	{
		ShowPrompt(false, "Hash fix failed after writing");
		size = 0;
	}
	
    if (cache) free(cache);
    return size;
}