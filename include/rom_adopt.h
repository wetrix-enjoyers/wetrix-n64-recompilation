/* The port's ROM: finding it, checking it, and moving it where it is read from.
 *
 * Implemented in src/rom_adopt.c, which explains why the step exists at all.
 * Compiled into both the exe and the build tool (tools/provision_rom.c) so that
 * a checkout and a handed-around port folder behave identically.
 */

#ifndef WETRIX_ROM_ADOPT_H
#define WETRIX_ROM_ADOPT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Ensure the ROM is at `target_dir`/wetrix.z64 -- the one name and place
 * librecomp loads from. If it is already there and correct, this is a single
 * read and no writes.
 *
 * Otherwise: if `explicit_source` is given, that file is the only candidate;
 * if it is NULL, `target_dir`, `target_dir/data` and up to four parent
 * directories are scanned for *.z64, *.n64 and *.v64 files of the right size.
 * Each candidate is byte-order normalised if needed, checked against the ROM
 * this port was recompiled from (internal name and XXH3 hash), and the first
 * match is moved into place -- renamed when its bytes are already what is
 * wanted, written out when they were reversed.
 *
 * Nothing is deleted: a dump that gets rewritten stays where the player put it.
 *
 * `prefix` is printed in front of every message, so the exe can keep its
 * "[wetrix] " convention and the tool can print plainly.
 *
 * Returns 0 when the file is in place and usable, non-zero otherwise. */
int wetrix_rom_adopt(const char* target_dir, const char* explicit_source, const char* prefix);

#ifdef __cplusplus
}
#endif

#endif /* WETRIX_ROM_ADOPT_H */
