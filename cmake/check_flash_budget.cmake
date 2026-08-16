# Fails the build if a target's flashed .bin image has grown large enough to
# overlap the flash address where the WHD resource pack gets loaded separately
# via `picotool load ... -o <TINY_WAD_ADDR>` (see README.md "Flashing the code
# and resources pack"). Without this check, an overflow here isn't caught at
# build time at all - it only shows up later as a runtime
# `panic("No WHD at %p\n", ...)` on the device, or worse, silent corruption if
# the overlap happens to leave the WHD magic bytes intact.
#
# Invoked as: cmake -DBIN_FILE=... -DWAD_ADDR=... -DFLASH_BASE=... -DTARGET_NAME=... -P check_flash_budget.cmake

if (NOT EXISTS "${BIN_FILE}")
    message(FATAL_ERROR "check_flash_budget: expected bin file not found: ${BIN_FILE}")
endif()

file(SIZE "${BIN_FILE}" BIN_SIZE)
math(EXPR BUDGET "${WAD_ADDR} - ${FLASH_BASE}")

if (BIN_SIZE GREATER BUDGET)
    math(EXPR OVERFLOW "${BIN_SIZE} - ${BUDGET}")
    message(FATAL_ERROR
            "${TARGET_NAME}: compiled firmware is ${BIN_SIZE} bytes, which overflows "
            "by ${OVERFLOW} bytes into the flash region reserved for the WHD resource "
            "pack at TINY_WAD_ADDR=${WAD_ADDR} (budget is ${BUDGET} bytes from "
            "${FLASH_BASE}). Flashing a WHD at that offset would now overlap the tail "
            "of the code. Either shrink the build or move TINY_WAD_ADDR further out "
            "for this target (and update README.md's flashing instructions to match).")
else()
    math(EXPR MARGIN "${BUDGET} - ${BIN_SIZE}")
    message(STATUS "${TARGET_NAME}: firmware ${BIN_SIZE} / ${BUDGET} bytes before WAD region (${MARGIN} bytes margin)")
endif()
