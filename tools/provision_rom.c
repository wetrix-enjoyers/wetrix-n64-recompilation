/* Find the port's ROM and put it where the port reads it.
 *
 *   wetrix_provision                     # look for a ROM, adopt the first match
 *   wetrix_provision <rom>               # adopt this one, whatever it is called
 *   wetrix_provision <rom> -o <dir>      # ... and put wetrix.z64 in <dir>
 *
 * The work is in src/rom_adopt.c, which the exe also calls: the exe does
 * the same thing on a folder that does not have the ROM yet, and sharing the
 * code is what keeps a checkout and a handed-around port folder behaving the
 * same way. This tool exists only because of the order things happen in -- the
 * recompiler reads the ROM image to produce the port's C, which is long before
 * there is an exe to run -- so the build side needs a way in that does not
 * involve the game already existing.
 *
 * Nothing is deleted. See rom_adopt.c for what "adopt" involves and why the
 * file ends up named wetrix.z64 in a particular folder.
 *
 * Build (xxHash is a submodule of N64ModernRuntime):
 *
 *   cmake -S tools -B build/tools
 *   cmake --build build/tools
 */

#include <stdio.h>
#include <string.h>

#include "rom_adopt.h"

int main(int argc, char** argv) {
    const char* rom = NULL;
    const char* dir = ".";

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--to") == 0) && i + 1 < argc) {
            dir = argv[++i];
        }
        else if (rom == NULL) {
            rom = argv[i];
        }
        else {
            fprintf(stderr, "unexpected argument %s\n", argv[i]);
            fprintf(stderr, "usage: %s [rom] [-o dir]\n", argv[0]);
            return 2;
        }
    }

    return wetrix_rom_adopt(dir, rom, "") == 0 ? 0 : 1;
}
