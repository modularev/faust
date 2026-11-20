/************************** BEGIN daisy-memory.h **************************
 FAUST Architecture File
 Copyright (C) 2026 Arev Imer
 ---------------------------------------------------------------------
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with this program; If not, see <http://www.gnu.org/licenses/>.

 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.
 ************************************************************************/

// Memory for FAUST programs on a Daisy Seed (STM32H750, libDaisy; compiled
// with -mem), fastest first (faust/dsp/tiered-memory-manager.h):
//
//  1. zones below FAUST_DAISY_SMALL bytes: the heap (malloc)
//  2. zones up to FAUST_DAISY_DTCM_MAX: an arena in DTCM (tightly coupled:
//     no wait states, not cached), FAUST_DAISY_DTCM bytes
//  3. zones up to FAUST_DAISY_AXI_MAX: an arena on the AXI SRAM the program
//     doesn't use (programs loaded by the Daisy bootloader, APP_TYPE
//     BOOT_SRAM: the code runs from AXI SRAM, the rest of its 480 kB is free)
//  4. zones up to FAUST_DAISY_HEAP_MAX: the heap, while
//     FAUST_DAISY_HEAP_RESERVE bytes stay free for the rest of the program
//  5. the rest: an arena on the SDRAM (64 MB, read through the 16 kB data
//     cache)
//
// libDaisy's heap has no limit of its own (newlib's minimal _sbrk): its end
// is FAUST_DAISY_HEAP_END (the D2 SRAM for BOOT_SRAM programs).
//
// The arenas are freed as a whole: call daisy_memory::reset() after the
// program's destroy(). Usage:
//
//   mydsp::fManager = &daisy_memory::manager();
//   mydsp::classInit(sample_rate);
//   mydsp* dsp = mydsp::create();
//   dsp->instanceInit(sample_rate);
//   ...
//   mydsp::destroy(dsp);
//   daisy_memory::reset();

#ifndef __daisy_memory__
#define __daisy_memory__

#include <malloc.h>
#include <stdint.h>
#include <unistd.h>

#include "faust/dsp/tiered-memory-manager.h"

// zones the manager tracks at a time (8 bytes each): mooSpace2 has 195
#ifndef FAUST_DAISY_ZONES
#define FAUST_DAISY_ZONES 256
#endif
#ifndef FAUST_DAISY_SMALL
#define FAUST_DAISY_SMALL 1024
#endif
#ifndef FAUST_DAISY_DTCM
#define FAUST_DAISY_DTCM 65536
#endif
#ifndef FAUST_DAISY_DTCM_MAX
#define FAUST_DAISY_DTCM_MAX 32768
#endif
#ifndef FAUST_DAISY_AXI_MAX
#define FAUST_DAISY_AXI_MAX 131072
#endif
#ifndef FAUST_DAISY_HEAP_MAX
#define FAUST_DAISY_HEAP_MAX 131072
#endif
#ifndef FAUST_DAISY_HEAP_RESERVE
#define FAUST_DAISY_HEAP_RESERVE 32768
#endif
#ifndef FAUST_DAISY_HEAP_END
#ifdef BOOT_APP
#define FAUST_DAISY_HEAP_END 0x30048000u   // end of the D2 SRAM (RAM_D2)
#else
#define FAUST_DAISY_HEAP_END 0x24080000u   // end of the AXI SRAM
#endif
#endif
#ifndef FAUST_DAISY_AXI_END
#define FAUST_DAISY_AXI_END 0x24078000u    // the last 32 kB: libDaisy's linker script leaves them out
#endif
#ifndef FAUST_DAISY_SDRAM_END
#define FAUST_DAISY_SDRAM_END 0xC4000000u  // 64 MB
#endif

// linker script symbols (libDaisy core/STM32H750IB_*.lds)
extern "C" char _sidata, _sdata, _edata;  // .data: its initial values follow the code
extern "C" char _esdram_bss;              // end of the program's own SDRAM data

namespace daisy_memory {

    inline uintptr_t align32(uintptr_t a) { return (a + 31) & ~uintptr_t(31); }

    // the heap: above the break, plus free blocks
    inline size_t heapFree()
    {
        uintptr_t brk = reinterpret_cast<uintptr_t>(sbrk(0));
        size_t above = brk < FAUST_DAISY_HEAP_END ? FAUST_DAISY_HEAP_END - brk : 0;
        return above + mallinfo().fordblks;
    }

    inline memory_arena& dtcm()
    {
        // its own section name: a static in an inline function can't share
        // one with plain variables (libDaisy takes .dtcmram_bss*)
        alignas(32) static uint8_t buffer[FAUST_DAISY_DTCM] __attribute__((section(".dtcmram_bss.faust_arena")));
        static memory_arena a(buffer, sizeof(buffer));
        return a;
    }

    // the AXI SRAM after the program's image (empty for a program in
    // internal flash: its data and heap are on the AXI SRAM)
    inline memory_arena& axi()
    {
#ifdef BOOT_APP
        static uintptr_t start = align32(reinterpret_cast<uintptr_t>(&_sidata) + (&_edata - &_sdata));
        static memory_arena a(reinterpret_cast<void*>(start), start < FAUST_DAISY_AXI_END ? FAUST_DAISY_AXI_END - start : 0);
#else
        static memory_arena a(nullptr, 0);
#endif
        return a;
    }

    inline memory_arena& sdram()
    {
        static uintptr_t start = align32(reinterpret_cast<uintptr_t>(&_esdram_bss));
        static memory_arena a(reinterpret_cast<void*>(start), FAUST_DAISY_SDRAM_END - start);
        return a;
    }

    // the tiers in this order: 0 heap (small), 1 DTCM, 2 AXI, 3 heap, 4 SDRAM
    inline tiered_memory_manager<5, FAUST_DAISY_ZONES>& manager()
    {
        static malloc_tier heap(heapFree);
        static tiered_memory_manager<5, FAUST_DAISY_ZONES> m;
        static bool ready = false;
        if (!ready) {
            m.addTier(heap, 0, FAUST_DAISY_SMALL - 1);
            m.addTier(dtcm(), FAUST_DAISY_SMALL, FAUST_DAISY_DTCM_MAX);
            m.addTier(axi(), FAUST_DAISY_SMALL, FAUST_DAISY_AXI_MAX);
            m.addTier(heap, FAUST_DAISY_SMALL, FAUST_DAISY_HEAP_MAX, FAUST_DAISY_HEAP_RESERVE);
            m.addTier(sdram());
            ready = true;
        }
        return m;
    }

    // after the program's destroy(): the arenas are empty again
    inline void reset()
    {
        dtcm().reset();
        axi().reset();
        sdram().reset();
    }

} // namespace daisy_memory

#endif
/************************** END daisy-memory.h **************************/
