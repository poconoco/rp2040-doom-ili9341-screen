/*
 * Simple Pico flash-backed file store for configuration files.
 *
 * This uses savegame flash slot 7 as a container for named file data.
 */

#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "picodoom.h"
#include "picofs.h"
#include "doom/p_saveg.h"
#include "picoflash.h"

#define PICOFS_MAGIC 0x50434653u /* 'PCFS' */
#define PICOFS_VERSION 1
#define PICOFS_CONFIG_SLOT 7
#define PICOFS_MAX_FILES 8

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_files;
    uint32_t dir_offset;
    uint32_t reserved;
} pico_fs_header_t;

typedef struct {
    uint32_t name_hash;
    uint32_t name_offset;
    uint32_t name_len;
    uint32_t file_offset;
    uint32_t file_len;
} pico_fs_entry_t;

typedef struct {
    const char *name;
    uint32_t name_len;
    uint32_t name_hash;
    const uint8_t *data;
    uint32_t file_len;
} pico_fs_file_t;

static uint32_t pico_fs_hash_name(const char *name)
{
    uint32_t hash = 0;

    while (*name != '\0')
    {
        hash = hash * 31u + (uint8_t)*name;
        name++;
    }

    return hash;
}

static const pico_fs_entry_t *pico_fs_find_entry(const uint8_t *container,
                                                  uint32_t container_size,
                                                  const char *name)
{
    if (container_size < sizeof(pico_fs_header_t))
    {
        return NULL;
    }

    const pico_fs_header_t *header = (const pico_fs_header_t *)container;
    if (header->magic != PICOFS_MAGIC || header->version != PICOFS_VERSION)
    {
        return NULL;
    }

    if (header->dir_offset != sizeof(pico_fs_header_t))
    {
        return NULL;
    }

    if (header->num_files > PICOFS_MAX_FILES)
    {
        return NULL;
    }

    const uint8_t *entries_base = container + sizeof(pico_fs_header_t);
    uint32_t required = sizeof(pico_fs_header_t) + header->num_files * sizeof(pico_fs_entry_t);
    if (required > container_size)
    {
        return NULL;
    }

    uint32_t name_hash = pico_fs_hash_name(name);

    for (uint32_t i = 0; i < header->num_files; ++i)
    {
        const pico_fs_entry_t *entry = (const pico_fs_entry_t *)(entries_base + i * sizeof(pico_fs_entry_t));

        if (entry->name_hash != name_hash)
        {
            continue;
        }
        if (entry->name_offset + entry->name_len > container_size)
        {
            continue;
        }
        if (entry->file_offset + entry->file_len > container_size)
        {
            continue;
        }

        const char *stored_name = (const char *)(container + entry->name_offset);
        if (entry->name_len == strlen(name)
            && memcmp(stored_name, name, entry->name_len) == 0)
        {
            return entry;
        }
    }

    return NULL;
}

static int pico_fs_load_files(const uint8_t *container, uint32_t container_size,
                              pico_fs_file_t *out, int max_files)
{
    if (container_size < sizeof(pico_fs_header_t))
    {
        return 0;
    }

    const pico_fs_header_t *header = (const pico_fs_header_t *)container;
    if (header->magic != PICOFS_MAGIC || header->version != PICOFS_VERSION)
    {
        return 0;
    }

    if (header->dir_offset != sizeof(pico_fs_header_t))
    {
        return 0;
    }

    if (header->num_files > (uint32_t)max_files)
    {
        return 0;
    }

    uint32_t required = sizeof(pico_fs_header_t) + header->num_files * sizeof(pico_fs_entry_t);
    if (required > container_size)
    {
        return 0;
    }

    const uint8_t *entries_base = container + sizeof(pico_fs_header_t);
    int count = 0;

    for (uint32_t i = 0; i < header->num_files; ++i)
    {
        const pico_fs_entry_t *entry = (const pico_fs_entry_t *)(entries_base + i * sizeof(pico_fs_entry_t));

        if (entry->name_offset + entry->name_len > container_size
            || entry->file_offset + entry->file_len > container_size)
        {
            return 0;
        }

        out[count].name = (const char *)(container + entry->name_offset);
        out[count].name_len = entry->name_len;
        out[count].name_hash = entry->name_hash;
        out[count].data = container + entry->file_offset;
        out[count].file_len = entry->file_len;
        count++;
    }

    return count;
}

boolean PicoFS_IsAvailable(void)
{
    size_t flash_size = get_end_of_flash() - (const uint8_t *)XIP_BASE;
    return flash_size >= 2u * 1024u * 1024u;
}

boolean PicoFS_FileExists(const char *filename)
{
    if (!PicoFS_IsAvailable() || filename == NULL)
    {
        return false;
    }

    flash_slot_info_t slots[8];
    P_SaveGameGetExistingFlashSlotAddresses(slots, 8);
    if (!slots[PICOFS_CONFIG_SLOT].data)
    {
        return false;
    }

    const pico_fs_entry_t *entry = pico_fs_find_entry(slots[PICOFS_CONFIG_SLOT].data,
                                                       slots[PICOFS_CONFIG_SLOT].size,
                                                       filename);
    return entry != NULL;
}

int PicoFS_ReadFile(const char *filename, byte **buffer)
{
    if (!PicoFS_IsAvailable() || filename == NULL || buffer == NULL)
    {
        return 0;
    }

    flash_slot_info_t slots[8];
    P_SaveGameGetExistingFlashSlotAddresses(slots, 8);
    if (!slots[PICOFS_CONFIG_SLOT].data)
    {
        return 0;
    }

    const pico_fs_entry_t *entry = pico_fs_find_entry(slots[PICOFS_CONFIG_SLOT].data,
                                                       slots[PICOFS_CONFIG_SLOT].size,
                                                       filename);
    if (entry == NULL)
    {
        return 0;
    }

    byte *copy = malloc(entry->file_len + 1);
    if (copy == NULL)
    {
        return 0;
    }

    memcpy(copy, (const uint8_t *)slots[PICOFS_CONFIG_SLOT].data + entry->file_offset,
           entry->file_len);
    copy[entry->file_len] = '\0';
    *buffer = copy;
    return (int)entry->file_len;
}

boolean PicoFS_SaveFile(const char *filename, const void *source, int length,
                         uint8_t *buffer4k)
{
    if (!PicoFS_IsAvailable() || filename == NULL || source == NULL || length < 0)
    {
        return false;
    }

    boolean allocated_buffer = false;
    if (buffer4k == NULL)
    {
        buffer4k = malloc(FLASH_SECTOR_SIZE);
        if (buffer4k == NULL)
        {
            return false;
        }
        allocated_buffer = true;
    }

    flash_slot_info_t slots[8];
    P_SaveGameGetExistingFlashSlotAddresses(slots, 8);

    pico_fs_file_t files[PICOFS_MAX_FILES + 1];
    int file_count = 0;

    if (slots[PICOFS_CONFIG_SLOT].data)
    {
        file_count = pico_fs_load_files(slots[PICOFS_CONFIG_SLOT].data,
                                       slots[PICOFS_CONFIG_SLOT].size,
                                       files, PICOFS_MAX_FILES);
        if (file_count < 0)
        {
            file_count = 0;
        }
    }

    uint32_t target_hash = pico_fs_hash_name(filename);
    bool replaced = false;
    for (int i = 0; i < file_count; ++i)
    {
        if (files[i].name_hash == target_hash
            && files[i].name_len == strlen(filename)
            && memcmp(files[i].name, filename, files[i].name_len) == 0)
        {
            files[i].data = (const uint8_t *)source;
            files[i].file_len = (uint32_t)length;
            replaced = true;
            break;
        }
    }

    if (!replaced)
    {
        if (file_count >= PICOFS_MAX_FILES)
        {
            return false;
        }
        files[file_count].name = filename;
        files[file_count].name_len = (uint32_t)strlen(filename);
        files[file_count].name_hash = target_hash;
        files[file_count].data = (const uint8_t *)source;
        files[file_count].file_len = (uint32_t)length;
        file_count++;
    }

    uint32_t dir_offset = sizeof(pico_fs_header_t);
    uint32_t entries_size = file_count * sizeof(pico_fs_entry_t);
    uint32_t data_offset = dir_offset + entries_size;
    uint32_t next_offset = data_offset;

    for (int i = 0; i < file_count; ++i)
    {
        next_offset += files[i].name_len;
    }

    for (int i = 0; i < file_count; ++i)
    {
        next_offset += files[i].file_len;
    }

    uint32_t total_size = next_offset;
    uint8_t *container = malloc(total_size);
    if (container == NULL)
    {
        return false;
    }

    pico_fs_header_t header = {
        .magic = PICOFS_MAGIC,
        .version = PICOFS_VERSION,
        .num_files = (uint32_t)file_count,
        .dir_offset = dir_offset,
        .reserved = 0,
    };
    memcpy(container, &header, sizeof(header));

    uint32_t name_write_offset = data_offset;
    uint32_t file_write_offset = data_offset;
    for (int i = 0; i < file_count; ++i)
    {
        file_write_offset += files[i].name_len;
    }

    for (int i = 0; i < file_count; ++i)
    {
        pico_fs_entry_t entry;
        entry.name_hash = files[i].name_hash;
        entry.name_offset = name_write_offset;
        entry.name_len = files[i].name_len;
        entry.file_offset = file_write_offset;
        entry.file_len = files[i].file_len;

        memcpy(container + dir_offset + i * sizeof(entry), &entry, sizeof(entry));
        memcpy(container + name_write_offset, files[i].name, files[i].name_len);
        memcpy(container + file_write_offset, files[i].data, files[i].file_len);

        name_write_offset += entry.name_len;
        file_write_offset += entry.file_len;
    }

    boolean result = P_SaveGameWriteFlashSlot(PICOFS_CONFIG_SLOT, container, total_size, buffer4k);
    free(container);
    if (allocated_buffer)
    {
        free(buffer4k);
    }
    return result;
}
