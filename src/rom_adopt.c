/* Finding the port's ROM, checking it, and moving it where the port reads it.
 *
 * The name is not a choice. librecomp derives the file it loads from the game id
 * -- GameEntry::stored_filename() is game_id + ".z64" -- and reads it out of the
 * registered config path, which main.cpp pins to the folder the exe lives in.
 * This port calls itself "wetrix", so the file it actually reads is
 *
 *     <folder containing wetrix.exe>/wetrix.z64
 *
 * and a ROM with any other name in any other place is invisible to it. That is
 * what this file bridges: it looks for a dump wherever one plausibly is, checks
 * it against the ROM the port was recompiled from, and gets it to that one name
 * and that one place.
 *
 * Two things call it, and they are deliberately the same code:
 *
 *   - the build tool, run once in the source tree, because N64Recomp reads the
 *     same file before there is an exe to run (see tools/provision_rom.c);
 *   - the exe, once, on a port folder that does not have the file yet -- the
 *     shape a folder gets handed around in, since nothing ROM-derived is
 *     committed and the ROM is the one part that cannot be shipped.
 *
 * What it will not do is remove or move the player's dump. The ROM is written to
 * the port's own name as big-endian -- copied as it is when it already is, byte
 * order fixed when it was reversed -- and the original is left exactly where it
 * was. The one removal in here is a file already sitting under the port's own
 * name that has been read and is not this ROM; the adopted file cannot take that
 * name otherwise.
 *
 * Every *.z64, *.n64 and *.v64 in the searched folders is a candidate, whatever
 * its size: the whole point is to say what is wrong with the file you actually
 * dropped rather than to silently skip it for looking unusual.
 *
 * Matching is the point of the step rather than a formality. The port's
 * recompiled C and its ROM map describe one specific ROM, and librecomp checks
 * the hash itself: fed a different revision it deletes the file and reports the
 * game as invalid, several frames into startup, with nothing to say about why.
 * So the check here is the same one it will make -- size, the internal name
 * field, and XXH3_64bits against the hash the port was built with -- applied to
 * every candidate before any is accepted, and the reason is printed for each one
 * that fails.
 *
 * Byte order is normalised on the way in, for the same reason the check is done
 * here: N64Recomp reads instruction words out of the file and this port's
 * runtime hashes what it finds, and neither tolerates a byte-reversed dump.
 * librecomp accepts all three orders when it is handed a file to select from,
 * but not when it loads the one already in place, so the normalising has to
 * happen before it lands there.
 */

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rom_adopt.h"

/* The check has to be the same computation librecomp makes, so it is the same
 * header -- and inlined the same way it inlines it: librecomp's recomp.cpp
 * compiles its copy with XXH_INLINE_ALL rather than linking a xxHash library,
 * and nothing in the runtime exports XXH3_64bits as a linkable symbol (RT64's
 * copies are static locals too). XXH_INLINE_ALL here means this call and the one
 * in check_hash are the same function body, which is the only way a hash
 * comparison across two translation units can be trusted to mean anything. */
#define XXH_INLINE_ALL
#include "xxhash.h"

/* The hash librecomp checks the file against, i.e. the GameEntry rom_hash in
 * the ports' main.cpp. Both are XXH3_64bits over the whole image. */
#define EXPECTED_HASH 0x7A64FDF6513D3659ULL

#define ROM_SIZE 0x800000
#define ROM_NAME_OFFSET 0x20
#define ROM_NAME_LENGTH 6 /* "Wetrix"; the 20-byte field is space-padded */
#define ROM_NAME "Wetrix"

/* The one name librecomp will look for. See the file header. */
#define ROM_FILENAME "wetrix.z64"

#define PATH_MAX_ 1024
#define MAX_CANDIDATES 32
#define MAX_READABLE_BYTES (64L * 1024L * 1024L)

/* How far up the tree to look. A checkout has the ROM at its root, two or three
 * levels above the exe in build/<config>/, and a handed-around port folder has
 * it next to the exe; four is comfortably past both and still bounded. */
#define MAX_ANCESTORS 4

enum byte_order {
    ORDER_BIG_ENDIAN,     /* .z64 -- already what the port wants */
    ORDER_SWAPPED_4,      /* .n64 -- byte-reversed within each word */
    ORDER_SWAPPED_2,      /* .v64 -- byte-reversed within each pair */
    ORDER_INVALID,
};

struct candidate {
    char path[PATH_MAX_];
};

static const char* log_prefix = "";

static void log_note(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s", log_prefix);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static enum byte_order detect_byte_order(const unsigned char* data, size_t size) {
    static const unsigned char magic[4] = { 0x80, 0x37, 0x12, 0x40 };

    if (size < 4) {
        return ORDER_INVALID;
    }
    if (memcmp(data, magic, 4) == 0) {
        return ORDER_BIG_ENDIAN;
    }
    for (int i = 0; i < 4; i++) {
        if (data[i] != magic[3 - i]) {
            break;
        }
        if (i == 3) {
            return ORDER_SWAPPED_4;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (data[i] != magic[i ^ 1]) {
            break;
        }
        if (i == 3) {
            return ORDER_SWAPPED_2;
        }
    }
    return ORDER_INVALID;
}

/* Byte order only ever changes within a 4-byte group, so both swapped cases are
 * the same permutation with a different index xor -- exactly as librecomp's
 * byteswap_data does it, so that what this accepts and what it accepts cannot
 * drift apart. */
static void normalise(unsigned char* data, size_t size, enum byte_order order) {
    size_t index_xor = (order == ORDER_SWAPPED_4) ? 3 : 1;

    for (size_t pos = 0; pos + 4 <= size; pos += 4) {
        unsigned char temp[4] = { data[pos + 0], data[pos + 1], data[pos + 2], data[pos + 3] };
        data[pos + (0 ^ index_xor)] = temp[0];
        data[pos + (1 ^ index_xor)] = temp[1];
        data[pos + (2 ^ index_xor)] = temp[2];
        data[pos + (3 ^ index_xor)] = temp[3];
    }
}

/* Case-insensitively, because a dump someone renamed by hand is as likely to be
 * ROM.Z64 as rom.z64 and the difference is not worth a failed search. */
static int has_rom_extension(const char* name) {
    const char* dot = strrchr(name, '.');
    if (dot == NULL || strlen(dot) != 4) {
        return 0;
    }
    if (dot[1] != 'z' && dot[1] != 'Z' && dot[1] != 'n' && dot[1] != 'N' &&
        dot[1] != 'v' && dot[1] != 'V') {
        return 0;
    }
    char second = (char)(dot[2] >= 'A' && dot[2] <= 'Z' ? dot[2] + 32 : dot[2]);
    char third = (char)(dot[3] >= 'A' && dot[3] <= 'Z' ? dot[3] + 32 : dot[3]);
    return second == '6' && third == '4';
}

static int is_regular_file(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
#ifdef _WIN32
    return (st.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(st.st_mode);
#endif
}

static long file_size_at(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (long)st.st_size;
}

static unsigned char* read_whole_file(const char* path, size_t* out_size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    rewind(file);
    if (size <= 0) {
        fclose(file);
        return NULL;
    }

    unsigned char* data = malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)size;
    return data;
}

/* Why a candidate was not accepted, phrased for the person who put it there.
 * Returns 1 when the bytes are the ROM this port was built for. */
static int describe_rejection(const unsigned char* data, size_t size, char* reason, size_t reason_size) {
    if (size != ROM_SIZE) {
        snprintf(reason, reason_size, "it is %ld bytes, and this port was recompiled from an %d-byte ROM",
                 (long)size, ROM_SIZE);
        return 0;
    }

    if (memcmp(data + ROM_NAME_OFFSET, ROM_NAME, ROM_NAME_LENGTH) != 0) {
        char name[ROM_NAME_LENGTH + 1];
        memcpy(name, data + ROM_NAME_OFFSET, ROM_NAME_LENGTH);
        name[ROM_NAME_LENGTH] = '\0';
        snprintf(reason, reason_size, "its internal name is \"%s\", not \"%s\"", name, ROM_NAME);
        return 0;
    }

    unsigned long long hash = (unsigned long long)XXH3_64bits(data, size);
    if (hash != EXPECTED_HASH) {
        snprintf(reason, reason_size,
                 "it is a different revision: hash 0x%016llX, and this port was built from 0x%016llX",
                 hash, (unsigned long long)EXPECTED_HASH);
        return 0;
    }

    reason[0] = '\0';
    return 1;
}

static int add_candidate(struct candidate* candidates, int count, const char* path) {
    if (count >= MAX_CANDIDATES) {
        return count;
    }
    if ((size_t)snprintf(candidates[count].path, sizeof(candidates[count].path), "%s", path) >=
        sizeof(candidates[count].path)) {
        return count;   /* too long to be worth handling; skip it */
    }
    return count + 1;
}

/* Everything in one directory. Not recursive on purpose: a dump gets dropped
 * next to the exe or at the root of a checkout, and walking a tree that holds a
 * 570MB dependency checkout would cost more than the search is worth. */
static int scan_directory(struct candidate* candidates, int count, const char* dir) {
    DIR* handle = opendir(dir);
    if (handle == NULL) {
        return count;
    }

    struct dirent* entry;
    char path[PATH_MAX_];
    while ((entry = readdir(handle)) != NULL) {
        if (!has_rom_extension(entry->d_name)) {
            continue;
        }
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= sizeof(path)) {
            continue;
        }
        if (!is_regular_file(path)) {
            continue;
        }
        /* No size filter. Telling a Wetrix ROM from another N64 ROM is the
         * hash's job, and it says precisely why it said no; a file that is the
         * wrong size is still worth reading far enough to say so. */
        count = add_candidate(candidates, count, path);
    }

    closedir(handle);
    return count;
}

static char* parent_directory(char* path) {
    char* slash = strrchr(path, '/');
    if (slash == NULL || slash == path) {
        return NULL;
    }
    *slash = '\0';
    return path;
}

/* Called once, from main, before any thread the runtime starts exists -- which is
 * what makes the two file-scope buffers below acceptable: they keep roughly 33KB
 * off the stack, and a second concurrent caller is not a thing. */
int wetrix_rom_adopt(const char* target_dir, const char* explicit_source, const char* prefix) {
    static char rom_path[PATH_MAX_];
    static struct candidate candidates[MAX_CANDIDATES];
    char reason[256];
    int count = 0;

    log_prefix = prefix != NULL ? prefix : "";

    if (snprintf(rom_path, sizeof(rom_path), "%s/" ROM_FILENAME, target_dir) >= (int)sizeof(rom_path)) {
        log_note("the path to " ROM_FILENAME " is too long: %s", target_dir);
        return 1;
    }

    /* The file the port reads, if it is already there and is the right ROM. One
     * read and one hash per launch, which is what librecomp is about to do with
     * it anyway; doing it here means a wrong file is reported rather than
     * silently deleted several frames later. */
    if (is_regular_file(rom_path)) {
        size_t size = 0;
        unsigned char* data = read_whole_file(rom_path, &size);
        if (data != NULL) {
            int usable = describe_rejection(data, size, reason, sizeof(reason));
            free(data);
            if (usable) {
                log_note("using %s", rom_path);
                return 0;
            }
            log_note("%s is not the ROM this port was built for: %s", rom_path, reason);
        }
    }

    if (explicit_source != NULL) {
        count = add_candidate(candidates, count, explicit_source);
        if (count == 0) {
            log_note("cannot use %s as a ROM path", explicit_source);
            return 1;
        }
    }
    else {
        /* Where a player would put it: beside the exe, in a data folder next to
         * it (what other ports in this space do), then up the tree, because a
         * checkout keeps it at the root and the exe is two levels down. */
        char dir[PATH_MAX_];
        if (snprintf(dir, sizeof(dir), "%s", target_dir) >= (int)sizeof(dir)) {
            log_note("the working path is too long: %s", target_dir);
            return 1;
        }

        count = scan_directory(candidates, count, dir);

        char data_dir[PATH_MAX_];
        if (snprintf(data_dir, sizeof(data_dir), "%s/data", dir) < (int)sizeof(data_dir)) {
            count = scan_directory(candidates, count, data_dir);
        }

        for (int level = 0; level < MAX_ANCESTORS; level++) {
            if (parent_directory(dir) == NULL) {
                break;
            }
            count = scan_directory(candidates, count, dir);
        }
    }

    for (int i = 0; i < count; i++) {
        /* The only upper bound applied anywhere, and it is about memory rather
         * than about the ROM: a dump large enough to be a disk image should not
         * be pulled into RAM to be told its size is wrong. */
        long candidate_size = file_size_at(candidates[i].path);
        if (candidate_size > MAX_READABLE_BYTES) {
            log_note("skipping %s: it is %ld bytes, and this port was recompiled from an "
                     "%d-byte ROM", candidates[i].path, candidate_size, ROM_SIZE);
            continue;
        }

        size_t size = 0;
        unsigned char* data = read_whole_file(candidates[i].path, &size);
        if (data == NULL) {
            log_note("cannot read %s", candidates[i].path);
            continue;
        }

        /* Size before byte order: a wrong-size file has no meaningful header to
         * read, and "it is 4194304 bytes" is the more useful of the two
         * complaints to lead with. */
        if (size != ROM_SIZE) {
            describe_rejection(data, size, reason, sizeof(reason));
            log_note("skipping %s: %s", candidates[i].path, reason);
            free(data);
            continue;
        }

        /* Report the byte order rather than accepting whatever it is: a wrong
         * guess here produces a file that looks fine and boots into garbage. */
        enum byte_order order = detect_byte_order(data, size);
        switch (order) {
            case ORDER_BIG_ENDIAN:
                break;
            case ORDER_SWAPPED_4:
            case ORDER_SWAPPED_2:
                normalise(data, size, order);
                break;
            case ORDER_INVALID:
                log_note("skipping %s: it does not start with an N64 header", candidates[i].path);
                free(data);
                continue;
        }

        if (!describe_rejection(data, size, reason, sizeof(reason))) {
            log_note("skipping %s: %s", candidates[i].path, reason);
            free(data);
            continue;
        }

        if (order != ORDER_BIG_ENDIAN) {
            log_note("%s is byte-reversed (%s); rewriting it big-endian as " ROM_FILENAME,
                     candidates[i].path, order == ORDER_SWAPPED_4 ? ".n64" : ".v64");
        }

        /* An invalid file may be sitting under the name the port reads. It was
         * just rejected above, so replacing it loses nothing -- but say so. */
        if (is_regular_file(rom_path)) {
            log_note("replacing %s, which is not this port's ROM", rom_path);
            if (remove(rom_path) != 0) {
                log_note("cannot remove %s", rom_path);
                free(data);
                return 1;
            }
        }

        FILE* out = fopen(rom_path, "wb");
        if (out == NULL) {
            log_note("cannot open %s for writing", rom_path);
            free(data);
            return 1;
        }
        if (fwrite(data, 1, size, out) != size || fclose(out) != 0) {
            log_note("cannot write %s", rom_path);
            remove(rom_path);
            free(data);
            return 1;
        }
        free(data);

        /* Read back what was written before claiming success: this is the one
         * path that rewrites bytes, and a truncated write would otherwise be
         * reported as a working ROM. */
        size_t written_size = 0;
        unsigned char* written = read_whole_file(rom_path, &written_size);
        if (written == NULL) {
            log_note("%s could not be read back", rom_path);
            return 1;
        }
        int ok = describe_rejection(written, written_size, reason, sizeof(reason));
        free(written);
        if (!ok) {
            log_note("%s was written but is not usable: %s", rom_path, reason);
            return 1;
        }

        log_note("wrote %s from %s; %s is untouched and stays where it is", rom_path,
                 candidates[i].path, candidates[i].path);
        return 0;
    }

    /* Nothing matched. Say what was looked for, since the fix is a file in the
     * right place and the message is the only instruction anyone gets. */
    if (count == 0) {
        log_note("no ROM found. Put your Wetrix ROM in %s (or in %s/data, or in any "
                 "folder above it) -- .z64, .n64 and .v64 all work -- and run again.",
                 target_dir, target_dir);
    }
    else {
        log_note("%d dump(s) were found but none is the ROM this port was recompiled from; "
                 "the reasons are above.", count);
    }
    return 1;
}
