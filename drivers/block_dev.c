#include <drivers/block_dev.h>
#include <mm/mm_heap.h>

int block_dev_read(block_dev_t *dev, u64 sector, u32 count, u8 *buffer) {
    if (!dev || !dev->read_sectors || !dev->sector_size ||
        (count && !buffer)) return -1;
    if (!count) return 0;
    if (sector >= dev->sector_count ||
        (u64)count > dev->sector_count - sector) return -1;
    return dev->read_sectors(dev, sector, count, buffer);
}

int block_dev_write(block_dev_t *dev, u64 sector, u32 count, const u8 *buffer) {
    if (!dev || !dev->write_sectors || !dev->sector_size ||
        (count && !buffer)) return -1;
    if (!count) return 0;
    if (sector >= dev->sector_count ||
        (u64)count > dev->sector_count - sector) return -1;
    return dev->write_sectors(dev, sector, count, buffer);
}
