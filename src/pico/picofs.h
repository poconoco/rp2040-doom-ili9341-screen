/*
 * Simple Pico flash-backed file store for configuration files.
 *
 * Supports storing multiple named files inside a reserved flash slot.
 */
#pragma once

#include "doomtype.h"

#ifdef PICO_ON_DEVICE
boolean PicoFS_IsAvailable(void);
boolean PicoFS_FileExists(const char *filename);
int PicoFS_ReadFile(const char *filename, byte **buffer);
boolean PicoFS_SaveFile(const char *filename, const void *source, int length, uint8_t *buffer4k);
#endif
