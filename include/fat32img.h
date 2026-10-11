// include/fat32.h —— 剥离Linux依赖，取自linux/msdos_fs.h
#ifndef _YUXOS_FAT32_H
#define _YUXOS_FAT32_H

#include <stdint.h>

/* 基础类型替代linux/types */
typedef uint8_t   __u8;
typedef uint16_t  __u16;
typedef uint32_t  __u32;
typedef int16_t   __s16;
typedef int32_t   __s32;

/* 小端访问辅助：QEMU/virtio-blk磁盘是little‑endian */
#define le16_to_cpu(v)  ((uint16_t)(v))
#define le32_to_cpu(v)  ((uint32_t)(v))
#define cpu_to_le16(v)  ((__le16)(v))
#define cpu_to_le32(v)  ((__le32)(v))

typedef uint16_t __le16;
typedef uint32_t __le32;

#define SECTOR_SIZE		512U	/* sector size (bytes) */
#define SECTOR_BITS		9U		/* log2(SECTOR_SIZE) */

/* 文件属性 */
#define ATTR_NONE	0U	/* no attribute bits */
#define ATTR_RO		1U	/* read‑only */
#define ATTR_HIDDEN	2U	/* hidden */
#define ATTR_SYS	4U	/* system */
#define ATTR_VOLUME	8U	/* volume label */
#define ATTR_DIR	16U	/* directory */
#define ATTR_ARCH	32U	/* archived */

#define ATTR_EXT	(ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)

#define CASE_LOWER_BASE	8U	/* base is lower case */
#define CASE_LOWER_EXT	16U	/* extension is lower case */

#define DELETED_FLAG	0xe5U	/* marks file as deleted when in name[0] */
#define IS_FREE(n)	(!(*(n)) || *(n) == DELETED_FLAG)

#define MSDOS_NAME	11U	/* 8.3短文件名最大长度 */
#define MSDOS_LONGNAME	256U	/* 长文件名最大 */

/* media of boot sector */
#define FAT_VALID_MEDIA(x)	(((0xF8U <= (x) && (x) <= 0xFFU)) || ((x) == 0xF0U))

/* start of data cluster's entry (number of reserved clusters) */
#define FAT_START_ENT	2U

/* maximum number of clusters */
#define MAX_FAT12	0xFF4U
#define MAX_FAT16	0xFFF4U
#define MAX_FAT32	0x0FFFFFF6U

/* bad cluster mark */
#define BAD_FAT12	0xFF7U
#define BAD_FAT16	0xFFF7U
#define BAD_FAT32	0x0FFFFFF7U

/* standard EOF */
#define EOF_FAT12	0xFFFU
#define EOF_FAT16	0xFFFFU
#define EOF_FAT32	0x0FFFFFFFU

#define FAT_ENT_FREE	(0U)
#define FAT_ENT_BAD	(BAD_FAT32)
#define FAT_ENT_EOF	(EOF_FAT32)

/* FAT32 FSINFO 签名 */
#define FAT_FSINFO_SIG1	0x41615252U
#define FAT_FSINFO_SIG2	0x61417272U

/* FAT引导扇区 BPB —— fat_boot_sector */
struct fat_boot_sector {
	__u8	ignored[3];	/* Boot strap short or near jump */
	__u8	system_id[8];	/* Name */
	__u8	sector_size[2];	/* bytes per logical sector */
	__u8	sec_per_clus;	/* sectors/cluster */
	__le16	reserved;	/* reserved sectors */
	__u8	fats;		/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	sectors[2];	/* number of sectors (<65536) */
	__u8	media;		/* media code */
	__le16	fat_length;	/* sectors/FAT for FAT12/FAT16 */
	__le16	secs_track;	/* sectors per track */
	__le16	heads;		/* number of heads */
	__le32	hidden;		/* hidden sectors */
	__le32	total_sect;	/* total sectors if sectors==0 (large volume) */

	/* ---- FAT32 扩展字段 ---- */
	__le32	fat32_length;	/* sectors/FAT for FAT32 */
	__le16	flags;		/* bit8: fat mirroring */
	__u8	version[2];	/* major, minor filesystem version */
	__le32	root_cluster;	/* first cluster of root directory */
	__le16	info_sector;	/* filesystem info sector number */
	__le16	backup_boot;	/* backup boot sector */
	__le16	reserved2[6];	/* Unused */
};

/* FAT32 FSINFO扇区 */
struct fat_boot_fsinfo {
	__le32   signature1;	/* 0x41615252 */
	__u8     reserved1[480];
	__le32   signature2;	/* 0x61417272 */
	__le32   free_clusters;	/* Free cluster count,‑1 unknown */
	__le32   next_cluster;	/* Most recently allocated cluster */
	__u8     reserved2[12];
};

/* MS‑DOS 8.3短名目录项 */
struct msdos_dir_entry {
	__u8	name[8],ext[3];	/* name and extension */
	__u8	attr;		/* attribute bits */
	__u8    lcase;		/* Case for base and extension */
	__u8	ctime_cs;	/* Creation time centiseconds 0‑199 */
	__le16	ctime;		/* Creation time */
	__le16	cdate;		/* Creation date */
	__le16	adate;		/* Last access date */
	__le16	starthi;	/* High 16‑bit cluster number (FAT32) */
	__le16	time,date,start;/* modify time,date, low16‑bit cluster */
	__le32	size;		/* file size bytes */
};

/* VFAT长文件名slot（每槽存13个UTF‑16字符） */
struct msdos_dir_slot {
	__u8    id;			/* sequence number for slot, bit7=last slot */
	__u8    name0_4[10];	/* first 5 UTF‑16LE chars */
	__u8    attr;		/* must be ATTR_EXT for longname slot */
	__u8    reserved;	/* always 0 */
	__u8    alias_checksum;	/* checksum of short 8.3 alias */
	__u8    name5_10[12];	/* next 6 UTF‑16LE chars */
	__le16  start;		/* reserved for longname slot, must be zero */
	__u8    name11_12[4];	/* last 2 UTF‑16LE chars */
};

/* 获取簇号：FAT32把簇号拆成 starthi + start */
static inline uint32_t fat_dir_entry_cluster(const struct msdos_dir_entry *de)
{
	return (((uint32_t)le16_to_cpu(de->starthi)) << 16U) | (uint32_t)le16_to_cpu(de->start);
}

#endif /* _YUXOS_FAT32_H */