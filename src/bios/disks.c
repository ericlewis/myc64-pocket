/*
 * Upstream MiSTer IEC drive service for Pocket data slots.
 */

#include "bios.h"

#define NUM_DRIVES 2
#define G64_NUM_TRACKS 84
#define G64_TRACK_OFFSET_TABLE 12

#define D64_TRACKS 35
#define D64_TOTAL_SECTORS 683
#define D64_INVALID_LBA 0xffff
#define D64_SECTOR_SIZE 256

#define T64_MAX_FILES 48
#define T64_ENTRY_BASE 64
#define T64_ENTRY_SIZE 32
#define T64_VIRTUAL_SIZE (D64_TOTAL_SECTORS * D64_SECTOR_SIZE)
#define DISK_CPU_BUFFER ((volatile uint32_t *)DISK_BRIDGE_BUFFER)

typedef enum {
  DISK_NONE = 0,
  DISK_D64 = 1,
  DISK_G64 = 2,
  DISK_D81 = 3,
  DISK_T64 = 4,
} disk_type_t;

typedef struct {
  uint32_t offset;
  uint16_t load_addr;
  uint16_t length;
  uint16_t first_lba;
  uint16_t blocks;
  uint8_t file_type;
  uint8_t name[16];
} t64_file_t;

typedef struct {
  uint16_t slot_id;
  uint32_t size;
  disk_type_t type;
  uint32_t g64_offsets[G64_NUM_TRACKS];
  uint16_t g64_sizes[G64_NUM_TRACKS];
  uint8_t t64_file_count;
  uint8_t t64_dir_sectors;
  t64_file_t t64_files[T64_MAX_FILES];
} disk_image_t;

static disk_image_t disks[NUM_DRIVES];

static volatile uint32_t status_bar_timeout;
static uint32_t last_status;
static uint32_t last_lba;
static uint8_t sector_buf[D64_SECTOR_SIZE];

static const uint16_t d64_start_sectors[D64_TRACKS + 1] = {
    0,   21,  42,  63,  84,  105, 126, 147, 168, 189, 210, 231,
    252, 273, 294, 315, 336, 357, 376, 395, 414, 433, 452, 471,
    490, 508, 526, 544, 562, 580, 598, 615, 632, 649, 666, 683,
};

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void clear_sector(void) {
  for (unsigned i = 0; i < D64_SECTOR_SIZE; i++)
    sector_buf[i] = 0;
}

static uint8_t is_g64(uint16_t slot_id) {
  uint8_t hdr[8];
  bridge_ds_read(slot_id, 0, sizeof(hdr), hdr);
  return hdr[0] == 'G' && hdr[1] == 'C' && hdr[2] == 'R' && hdr[3] == '-' &&
         hdr[4] == '1' && hdr[5] == '5' && hdr[6] == '4' && hdr[7] == '1';
}

static uint8_t is_t64(uint16_t slot_id) {
  static const char sig[] = "C64 tape image file";
  uint8_t hdr[sizeof(sig) - 1];
  bridge_ds_read(slot_id, 0, sizeof(hdr), hdr);

  for (unsigned i = 0; i < sizeof(hdr); i++) {
    if (hdr[i] != (uint8_t)sig[i])
      return 0;
  }
  return 1;
}

static disk_type_t detect_disk_type(uint16_t slot_id, uint32_t size) {
  if (!size)
    return DISK_NONE;
  if (is_g64(slot_id))
    return DISK_G64;
  if (is_t64(slot_id))
    return DISK_T64;
  if (size == 819200)
    return DISK_D81;
  return DISK_D64;
}

static uint8_t upstream_img_type(disk_type_t type) {
  switch (type) {
  case DISK_G64:
    return 1; // direct GCR
  case DISK_D81:
    return 2; // 1581
  case DISK_D64:
  case DISK_T64:
  default:
    return 0; // sector-backed 1541
  }
}

static uint32_t mounted_size(const disk_image_t *disk) {
  return disk->type == DISK_T64 ? T64_VIRTUAL_SIZE : disk->size;
}

static uint8_t lba_to_ts(uint16_t lba, uint8_t *track, uint8_t *sector) {
  if (lba >= D64_TOTAL_SECTORS)
    return 0;

  for (uint8_t t = 1; t <= D64_TRACKS; t++) {
    if (lba < d64_start_sectors[t]) {
      *track = t;
      *sector = lba - d64_start_sectors[t - 1];
      return 1;
    }
  }
  return 0;
}

static uint16_t ts_to_lba(uint8_t track, uint8_t sector) {
  if (track < 1 || track > D64_TRACKS)
    return D64_INVALID_LBA;
  if (sector >= d64_start_sectors[track] - d64_start_sectors[track - 1])
    return D64_INVALID_LBA;
  return d64_start_sectors[track - 1] + sector;
}

static uint16_t t64_next_data_lba(uint16_t lba) {
  while (lba < D64_TOTAL_SECTORS) {
    uint8_t track, sector;
    if (!lba_to_ts(lba, &track, &sector))
      return D64_INVALID_LBA;
    if (track != 18)
      return lba;
    lba++;
  }
  return D64_INVALID_LBA;
}

static uint16_t t64_file_lba_at(const t64_file_t *file, uint16_t sector_idx) {
  uint16_t lba = file->first_lba;
  for (uint16_t i = 0; i < sector_idx && lba != D64_INVALID_LBA; i++)
    lba = t64_next_data_lba(lba + 1);
  return lba;
}

static int t64_find_file_sector(const disk_image_t *disk, uint16_t lba,
                                uint16_t *sector_idx) {
  for (uint8_t file_idx = 0; file_idx < disk->t64_file_count; file_idx++) {
    const t64_file_t *file = &disk->t64_files[file_idx];
    for (uint16_t i = 0; i < file->blocks; i++) {
      if (t64_file_lba_at(file, i) == lba) {
        *sector_idx = i;
        return file_idx;
      }
    }
  }
  return -1;
}

static uint8_t t64_reserve_blocks(uint16_t *next_lba, uint16_t blocks,
                                  uint16_t *first_lba) {
  uint16_t lba = t64_next_data_lba(*next_lba);
  if (lba == D64_INVALID_LBA)
    return 0;

  *first_lba = lba;
  for (uint16_t i = 0; i < blocks; i++) {
    if (lba == D64_INVALID_LBA)
      return 0;
    lba = t64_next_data_lba(lba + 1);
  }

  *next_lba = lba;
  return 1;
}

static void parse_g64(disk_image_t *disk) {
  for (unsigned i = 0; i < G64_NUM_TRACKS; i++) {
    uint32_t off =
        bridge_ds_get_uint32(disk->slot_id, G64_TRACK_OFFSET_TABLE + i * 4);
    disk->g64_offsets[i] = off;
    disk->g64_sizes[i] = off ? bridge_ds_get_uint16(disk->slot_id, off) : 0;
  }
}

static void parse_t64(disk_image_t *disk) {
  uint16_t total_entries = bridge_ds_get_uint16(disk->slot_id, 34);
  uint16_t max_entries = 0;
  uint16_t next_lba = 0;

  disk->t64_file_count = 0;
  disk->t64_dir_sectors = 1;

  if (disk->size > T64_ENTRY_BASE)
    max_entries = (disk->size - T64_ENTRY_BASE) / T64_ENTRY_SIZE;
  if (total_entries > max_entries)
    total_entries = max_entries;

  for (uint16_t idx = 0; idx < total_entries; idx++) {
    uint8_t entry[T64_ENTRY_SIZE];
    bridge_ds_read(disk->slot_id, T64_ENTRY_BASE + idx * T64_ENTRY_SIZE,
                   sizeof(entry), entry);

    if (entry[0] != 1 && entry[0] != 2)
      continue;

    uint16_t load_addr = le16(&entry[2]);
    uint16_t end_addr = le16(&entry[4]);
    uint32_t offset = le32(&entry[8]);
    if (end_addr <= load_addr || offset >= disk->size)
      continue;

    uint32_t length32 = end_addr - load_addr;
    if (length32 > disk->size - offset)
      length32 = disk->size - offset;
    if (!length32 || length32 > 0xffff)
      continue;

    uint32_t stream_len = length32 + 2;
    uint16_t blocks = (stream_len + 253) / 254;
    uint16_t first_lba;
    if (!t64_reserve_blocks(&next_lba, blocks, &first_lba))
      break;

    if (disk->t64_file_count >= T64_MAX_FILES)
      break;

    t64_file_t *file = &disk->t64_files[disk->t64_file_count++];
    file->offset = offset;
    file->load_addr = load_addr;
    file->length = length32;
    file->first_lba = first_lba;
    file->blocks = blocks;
    file->file_type = 0x80 | (entry[1] & 0x07);
    if ((file->file_type & 0x07) == 0)
      file->file_type = 0x82;

    for (unsigned i = 0; i < 16; i++)
      file->name[i] = entry[16 + i];
  }

  disk->t64_dir_sectors = (disk->t64_file_count + 7) / 8;
  if (!disk->t64_dir_sectors)
    disk->t64_dir_sectors = 1;
  if (disk->t64_dir_sectors > 18)
    disk->t64_dir_sectors = 18;
}

static void write_mount_regs(unsigned drive, const disk_image_t *disk) {
  volatile uint32_t *size_reg = drive ? DISK1_SIZE : DISK0_SIZE;
  volatile uint32_t *mount_reg = drive ? DISK1_MOUNT : DISK0_MOUNT;

  *size_reg = mounted_size(disk);
  if (disk->type == DISK_NONE) {
    *mount_reg = 0;
  } else {
    uint32_t readonly = disk->type == DISK_T64 ? 2 : 0;
    *mount_reg = 1 | readonly | (upstream_img_type(disk->type) << 2);
  }
}

static void mount_disk(unsigned drive) {
  disk_image_t *disk = &disks[drive];
  disk->size = bridge_ds_get_length(disk->slot_id);
  disk->type = detect_disk_type(disk->slot_id, disk->size);
  disk->t64_file_count = 0;
  disk->t64_dir_sectors = 1;

  if (disk->type == DISK_G64)
    parse_g64(disk);
  else if (disk->type == DISK_T64)
    parse_t64(disk);

  write_mount_regs(drive, disk);
}

void disks_init(void) {
  disks[0].slot_id = DISK8_SLOT_ID;
  disks[1].slot_id = DISK9_SLOT_ID;
  mount_disk(0);
  mount_disk(1);
}

static void ack_disk(uint16_t valid_len, uint8_t empty_gcr,
                     uint8_t from_cpu_buffer) {
  *DISK_ACK = ((uint32_t)valid_len << 16) | (from_cpu_buffer ? 4 : 0) |
              (empty_gcr ? 2 : 0) | 1;
}

static uint16_t clamp_len(uint32_t offset, uint16_t req_len, uint32_t size) {
  if (offset >= size)
    return 0;
  uint32_t remain = size - offset;
  return remain < req_len ? remain : req_len;
}

static void service_linear(disk_image_t *disk, uint8_t write, uint32_t lba,
                           uint16_t req_len) {
  uint32_t offset = lba << 8;
  uint16_t len = clamp_len(offset, req_len, disk->size);

  if (len) {
    if (write) {
      bridge_ds_write_direct(disk->slot_id, offset, len, DISK_BRIDGE_BUFFER);
      bridge_ds_flush(disk->slot_id);
    } else {
      bridge_ds_read_direct(disk->slot_id, offset, len, DISK_BRIDGE_BUFFER);
    }
  }

  ack_disk(len, 0, 0);
}

static void service_g64(disk_image_t *disk, uint8_t write, uint32_t lba,
                        uint16_t req_len) {
  if (lba >= G64_NUM_TRACKS || !disk->g64_offsets[lba] ||
      !disk->g64_sizes[lba]) {
    ack_disk(0, 1, 0);
    return;
  }

  uint32_t offset = disk->g64_offsets[lba];
  uint16_t len = disk->g64_sizes[lba] + 2;
  if (len > req_len)
    len = req_len;

  if (write) {
    bridge_ds_write_direct(disk->slot_id, offset, len, DISK_BRIDGE_BUFFER);
    bridge_ds_flush(disk->slot_id);
  } else {
    bridge_ds_read_direct(disk->slot_id, offset, len, DISK_BRIDGE_BUFFER);
  }

  ack_disk(len, 0, 0);
}

static void disk_cpu_buffer_write_sector(uint16_t offset) {
  volatile uint32_t *dst = (volatile uint32_t *)(DISK_BRIDGE_BUFFER + offset);

  for (unsigned i = 0; i < D64_SECTOR_SIZE; i += 4) {
    *dst++ = ((uint32_t)sector_buf[i] << 24) |
             ((uint32_t)sector_buf[i + 1] << 16) |
             ((uint32_t)sector_buf[i + 2] << 8) | sector_buf[i + 3];
  }
}

static uint8_t t64_sector_used(const disk_image_t *disk, uint8_t track,
                               uint8_t sector) {
  if (track == 18 && sector <= disk->t64_dir_sectors)
    return 1;

  uint16_t lba = ts_to_lba(track, sector);
  if (lba == D64_INVALID_LBA)
    return 0;

  uint16_t sector_idx;
  return t64_find_file_sector(disk, lba, &sector_idx) >= 0;
}

static void build_t64_bam_sector(const disk_image_t *disk) {
  clear_sector();
  sector_buf[0] = 18;
  sector_buf[1] = 1;
  sector_buf[2] = 'A';

  for (uint8_t track = 1; track <= D64_TRACKS; track++) {
    uint8_t sectors = d64_start_sectors[track] - d64_start_sectors[track - 1];
    uint32_t bitmap = (1UL << sectors) - 1;
    uint8_t free_count = sectors;

    for (uint8_t sector = 0; sector < sectors; sector++) {
      if (t64_sector_used(disk, track, sector)) {
        bitmap &= ~(1UL << sector);
        free_count--;
      }
    }

    unsigned base = 4 + (track - 1) * 4;
    sector_buf[base] = free_count;
    sector_buf[base + 1] = bitmap & 0xff;
    sector_buf[base + 2] = (bitmap >> 8) & 0xff;
    sector_buf[base + 3] = (bitmap >> 16) & 0xff;
  }

  static const char disk_name[] = "POCKET T64";
  for (unsigned i = 0; i < 16; i++)
    sector_buf[0x90 + i] = i < sizeof(disk_name) - 1 ? disk_name[i] : 0xa0;
  for (unsigned i = 0xa0; i <= 0xab; i++)
    sector_buf[i] = 0xa0;
  sector_buf[0xa0] = 'P';
  sector_buf[0xa1] = 'K';
  sector_buf[0xa2] = '2';
  sector_buf[0xa3] = 'A';
}

static void build_t64_dir_sector(const disk_image_t *disk, uint8_t dir_sector) {
  clear_sector();

  if (dir_sector + 1 < disk->t64_dir_sectors) {
    sector_buf[0] = 18;
    sector_buf[1] = 2 + dir_sector;
  } else {
    sector_buf[0] = 0;
    sector_buf[1] = 0xff;
  }

  for (uint8_t entry = 0; entry < 8; entry++) {
    uint8_t file_idx = dir_sector * 8 + entry;
    if (file_idx >= disk->t64_file_count)
      break;

    const t64_file_t *file = &disk->t64_files[file_idx];
    uint8_t track, sector;
    lba_to_ts(file->first_lba, &track, &sector);

    unsigned base = entry * 32;
    sector_buf[base + 2] = file->file_type;
    sector_buf[base + 3] = track;
    sector_buf[base + 4] = sector;
    for (unsigned i = 0; i < 16; i++) {
      uint8_t c = file->name[i];
      sector_buf[base + 5 + i] = (c == 0 || c == ' ') ? 0xa0 : c;
    }
    sector_buf[base + 30] = file->blocks & 0xff;
    sector_buf[base + 31] = file->blocks >> 8;
  }
}

static void build_t64_file_sector(const disk_image_t *disk,
                                  const t64_file_t *file,
                                  uint16_t sector_idx) {
  clear_sector();

  uint32_t stream_len = (uint32_t)file->length + 2;
  uint32_t stream_pos = (uint32_t)sector_idx * 254;
  uint16_t used = 0;

  if (stream_pos < stream_len)
    used = MIN(254, stream_len - stream_pos);

  if (sector_idx + 1 < file->blocks) {
    uint16_t next_lba = t64_file_lba_at(file, sector_idx + 1);
    uint8_t track, sector;
    lba_to_ts(next_lba, &track, &sector);
    sector_buf[0] = track;
    sector_buf[1] = sector;
  } else {
    sector_buf[0] = 0;
    sector_buf[1] = used + 1;
  }

  uint16_t dst = 2;
  while (used) {
    if (stream_pos == 0) {
      sector_buf[dst++] = file->load_addr & 0xff;
      stream_pos++;
      used--;
    } else if (stream_pos == 1) {
      sector_buf[dst++] = file->load_addr >> 8;
      stream_pos++;
      used--;
    } else {
      uint16_t chunk = used;
      bridge_ds_read(disk->slot_id, file->offset + stream_pos - 2, chunk,
                     &sector_buf[dst]);
      dst += chunk;
      stream_pos += chunk;
      used -= chunk;
    }
  }
}

static void build_t64_sector(const disk_image_t *disk, uint32_t lba) {
  uint8_t track, sector;
  if (!lba_to_ts(lba, &track, &sector)) {
    clear_sector();
    return;
  }

  if (track == 18 && sector == 0) {
    build_t64_bam_sector(disk);
    return;
  }

  if (track == 18 && sector >= 1 && sector <= disk->t64_dir_sectors) {
    build_t64_dir_sector(disk, sector - 1);
    return;
  }

  uint16_t sector_idx;
  int file_idx = t64_find_file_sector(disk, lba, &sector_idx);
  if (file_idx >= 0) {
    build_t64_file_sector(disk, &disk->t64_files[file_idx], sector_idx);
    return;
  }

  clear_sector();
}

static void service_t64(disk_image_t *disk, uint8_t write, uint32_t lba,
                        uint16_t req_len) {
  if (write) {
    ack_disk(req_len, 0, 0);
    return;
  }

  for (uint16_t offset = 0; offset < req_len; offset += D64_SECTOR_SIZE) {
    build_t64_sector(disk, lba + (offset >> 8));
    disk_cpu_buffer_write_sector(offset);
  }

  ack_disk(req_len, 0, 1);
}

void disks_poll(void) {
  uint32_t status = *DISK_STATUS;
  if (!(status & 1))
    return;

  uint8_t write = (status >> 1) & 1;
  unsigned drive = (status >> 2) & 3;
  uint32_t lba = *DISK_LBA;
  uint16_t req_len = (((*DISK_BLK_CNT) & 0x3f) + 1) << 8;

  last_status = status;
  last_lba = lba;

  if (drive >= NUM_DRIVES || disks[drive].type == DISK_NONE) {
    ack_disk(0, 0, 0);
    return;
  }

  if (disks[drive].type == DISK_G64)
    service_g64(&disks[drive], write, lba, req_len);
  else if (disks[drive].type == DISK_T64)
    service_t64(&disks[drive], write, lba, req_len);
  else
    service_linear(&disks[drive], write, lba, req_len);
}

void disks_irq(void) {
  if (updated_slots & (1 << DISK8_SLOT_ID))
    mount_disk(0);
  if (updated_slots & (1 << DISK9_SLOT_ID))
    mount_disk(1);

  uint32_t status = *DISK_STATUS;
  if ((status >> 6) & 3) {
    osd_mode = OSD_STATUS_BAR;
    status_bar_timeout = timer_ticks + 300;
  }
  if (osd_mode == OSD_STATUS_BAR && timer_ticks > status_bar_timeout) {
    osd_mode = OSD_OFF;
  }
}

void disks_draw_status_bar(void) {
  uint8_t drive = (last_status >> 2) & 1;
  uint8_t write = (last_status >> 1) & 1;
  const char *type = "NONE";

  switch (disks[drive].type) {
  case DISK_D64:
    type = "D64";
    break;
  case DISK_G64:
    type = "G64";
    break;
  case DISK_D81:
    type = "D81";
    break;
  case DISK_T64:
    type = "T64";
    break;
  default:
    break;
  }

  osd_printf(0, 0, "[D%d %s %c LBA:%x] ", drive + 8, type,
             write ? 'W' : 'R', last_lba);
}
