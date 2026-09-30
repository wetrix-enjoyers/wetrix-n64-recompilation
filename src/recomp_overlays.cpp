// Registration of the generated section / overlay tables.
//
// N64Recomp emits recomp_overlays.inl, but the file is data only: it defines the
// section table, the per-section function tables, and the overlay index map, and
// then stops. Handing them to the runtime is the port's job, and nothing did it,
// which is why the tables were inert.
//
// The consequence is not subtle. recomp::overlays::init_overlays() sizes and
// sorts whatever is in sections_info, and func_map -- the address-to-function
// table that get_function() consults -- is only ever filled by walking those
// sections. With sections_info left zeroed there are no sections to walk, so
// func_map stays empty and the first thread the ROM creates dies in
// run_thread_function() with:
//
//     Failed to find function at 0x80031EB4
//
// 0x80031EB4 is Thread_0A_1 out of config/us/symbol_addrs.txt. It is a perfectly
// ordinary recompiled function; the map simply had nothing in it.
//
// Nothing below is Wetrix-specific -- the three tables come straight out of the
// .inl and only the ARRLEN-style sizes are computed here. num_sections is
// emitted separately because it counts every section in the ELF, not just the
// code sections listed in section_table; the runtime uses it to size the
// per-section address array, which is indexed by section index.

#include "recomp.h"
#include "librecomp/overlays.hpp"

// Defines section_table, overlay_sections_by_index and num_sections. Pulls in
// funcs.h for the function pointers and librecomp/sections.h for the entry
// types and ARRLEN.
#include "recomp_overlays.inl"

void wetrix_register_overlays() {
    recomp::overlays::register_overlays(
        recomp::overlays::overlay_section_table_data_t{
            .code_sections = section_table,
            .num_code_sections = ARRLEN(section_table),
            .total_num_sections = num_sections,
        },
        recomp::overlays::overlays_by_index_t{
            .table = overlay_sections_by_index,
            .len = ARRLEN(overlay_sections_by_index),
        });
}
