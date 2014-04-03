/*
 * Программатор flash-памяти для микроконтроллеров Миландр ARM Cortex-M3.
 * Автор: С.Вакуленко.
 *
 * Этот файл распространяется в надежде, что он окажется полезным, но
 * БЕЗ КАКИХ БЫ ТО НИ БЫЛО ГАРАНТИЙНЫХ ОБЯЗАТЕЛЬСТВ; в том числе без косвенных
 * гарантийных обязательств, связанных с ПОТРЕБИТЕЛЬСКИМИ СВОЙСТВАМИ и
 * ПРИГОДНОСТЬЮ ДЛЯ ОПРЕДЕЛЕННЫХ ЦЕЛЕЙ.
 *
 * Вы вправе распространять и/или изменять этот файл в соответствии
 * с условиями Генеральной Общественной Лицензии GNU (GPL) в том виде,
 * как она была опубликована Фондом Свободного ПО; либо версии 2 Лицензии
 * либо (по вашему желанию) любой более поздней версии. Подробности
 * смотрите в прилагаемом файле 'COPYING.txt'.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>
#include <locale.h>

#include "target.h"
#include "localize.h"

#define VERSION         "1.1"
#define BLOCKSZ         4096 //1024
#define FLASH_BLOCK_SZ	4096
#define DEFAULT_ADDR    0x08000000

/* Macros for converting between hex and binary. */
#define NIBBLE(x)       (isdigit(x) ? (x)-'0' : tolower(x)+10-'a')
#define HEX(buffer)     ((NIBBLE((buffer)[0])<<4) + NIBBLE((buffer)[1]))

#define BLOCK_PROG_NB_RETRIES   3

unsigned char memory_data [0x20000];   /* Code - up to 128 kbytes */
int memory_len;
unsigned memory_base;
unsigned progress_count, progress_step;
int verify_only;
int prog_then_verify;
int debug_level;
target_t *target;
char *progname;
const char *copyright;

void *fix_time ()
{
    static struct timeval t0;

    gettimeofday (&t0, 0);
    return &t0;
}

unsigned mseconds_elapsed (void *arg)
{
    struct timeval t1, *t0 = arg;
    unsigned mseconds;

    gettimeofday (&t1, 0);
    mseconds = (t1.tv_sec - t0->tv_sec) * 1000 +
        (t1.tv_usec - t0->tv_usec) / 1000;
    if (mseconds < 1)
        mseconds = 1;
    return mseconds;
}

/*
 * Read binary file.
 */
int read_bin (char *filename, unsigned char *output)
{
    FILE *fd;
    int output_len;

    fd = fopen (filename, "rb");
    if (! fd) {
        perror (filename);
        exit (1);
    }
    output_len = fread (output, 1, sizeof (memory_data), fd);
    fclose (fd);
    if (output_len < 0) {
        fprintf (stderr, _("%s: read error\n"), filename);
        exit (1);
    }
    return output_len;
}

/*
 * Read the S record file.
 */
int read_srec (char *filename, unsigned char *output)
{
    FILE *fd;
    unsigned char buf [256];
    unsigned char *data;
    unsigned address;
    int bytes, output_len;

    fd = fopen (filename, "r");
    if (! fd) {
        perror (filename);
        exit (1);
    }
    output_len = 0;
    while (fgets ((char*) buf, sizeof(buf), fd)) {
        if (buf[0] == '\n')
            continue;
        if (buf[0] != 'S') {
            if (output_len == 0)
                break;
            fprintf (stderr, _("%s: bad SREC file format\n"), filename);
            exit (1);
        }
        if (buf[1] == '7' || buf[1] == '8' || buf[1] == '9')
            break;

        /* Starting an S-record.  */
        if (! isxdigit (buf[2]) || ! isxdigit (buf[3])) {
            fprintf (stderr, _("%s: bad record: %s\n"), filename, buf);
            exit (1);
        }
        bytes = HEX (buf + 2);

        /* Ignore the checksum byte.  */
        --bytes;

        address = 0;
        data = buf + 4;
        switch (buf[1]) {
        case '3':
            address = HEX (data);
            data += 2;
            --bytes;
            /* Fall through.  */
        case '2':
            address = (address << 8) | HEX (data);
            data += 2;
            --bytes;
            /* Fall through.  */
        case '1':
            address = (address << 8) | HEX (data);
            data += 2;
            address = (address << 8) | HEX (data);
            data += 2;
            bytes -= 2;

            if (! memory_base) {
                /* Автоматическое определение базового адреса. */
                memory_base = address;
            }
            if (address < memory_base) {
                fprintf (stderr, _("%s: incorrect address %08X, must be %08X or greater\n"),
                    filename, address, memory_base);
                exit (1);
            }
            address -= memory_base;
            if (address+bytes > sizeof (memory_data)) {
                fprintf (stderr, _("%s: address too large: %08X + %08X\n"),
                    filename, address + memory_base, bytes);
                exit (1);
            }
            while (bytes-- > 0) {
                output[address++] = HEX (data);
                data += 2;
            }
            if (output_len < (int) address)
                output_len = address;
            break;
        }
    }
    fclose (fd);
    return output_len;
}

/*
 * Read HEX file.
 */
int read_hex (char *filename, unsigned char *output)
{
    FILE *fd;
    unsigned char buf [256], data[16], record_type, sum;
    unsigned address, high;
    int bytes, output_len, i;

    fd = fopen (filename, "r");
    if (! fd) {
        perror (filename);
        exit (1);
    }
    output_len = 0;
    high = 0;
    while (fgets ((char*) buf, sizeof(buf), fd)) {
        if (buf[0] == '\n')
            continue;
        if (buf[0] != ':') {
            if (output_len == 0)
                break;
            fprintf (stderr, _("%s: bad HEX file format\n"), filename);
            exit (1);
        }
        if (! isxdigit (buf[1]) || ! isxdigit (buf[2]) ||
            ! isxdigit (buf[3]) || ! isxdigit (buf[4]) ||
            ! isxdigit (buf[5]) || ! isxdigit (buf[6]) ||
            ! isxdigit (buf[7]) || ! isxdigit (buf[8])) {
            fprintf (stderr, _("%s: bad record: %s\n"), filename, buf);
            exit (1);
        }
        record_type = HEX (buf+7);
        if (record_type == 1) {
        /* End of file. */
            break;
        }
        if (record_type == 5) {
            /* Start address, ignore. */
            continue;
        }

        bytes = HEX (buf+1);
        if (bytes & 1) {
            fprintf (stderr, _("%s: odd length\n"), filename);
            exit (1);
        }
        if (strlen ((char*) buf) < bytes * 2 + 11) {
            fprintf (stderr, _("%s: too short hex line\n"), filename);
            exit (1);
        }
        address = high << 16 | HEX (buf+3) << 8 | HEX (buf+5);
        if (address & 3) {
            fprintf (stderr, _("%s: odd address\n"), filename);
            exit (1);
        }

        sum = 0;
        for (i=0; i<bytes; ++i) {
            data [i] = HEX (buf+9 + i + i);
            sum += data [i];
        }
        sum += record_type + bytes + (address & 0xff) + (address >> 8 & 0xff);
        if (sum != (unsigned char) - HEX (buf+9 + bytes + bytes)) {
            fprintf (stderr, _("%s: bad hex checksum\n"), filename);
            exit (1);
        }
        
        if (record_type == 4) {
        /* Extended address. */
            if (bytes != 2) {
                fprintf (stderr, _("%s: invalid hex linear address record length\n"), filename);
                exit (1);
            }
            high = data[0] << 8 | data[1];
            continue;
        }
        
        if (record_type != 0) {
            fprintf (stderr, _("%s: unknown hex record type: %d\n"),
                filename, record_type);
            exit (1);
        }

        /* Data record found. */
        if (! memory_base) {
            /* Автоматическое определение базового адреса. */
            memory_base = address;
        }
        if (address < memory_base) {
            fprintf (stderr, _("%s: incorrect address %08X, must be %08X or greater\n"),
                filename, address, memory_base);
            exit (1);
        }
        address -= memory_base;
        if (address+bytes > sizeof (memory_data)) {
            fprintf (stderr, _("%s: address too large: %08X + %08X\n"),
                filename, address + memory_base, bytes);
            exit (1);
        }
        for (i=0; i<bytes; i++) {
            output[address++] = data [i];
        }
        if (output_len < (int) address)
            output_len = address;
    }
    fclose (fd);
    return output_len;
}

void print_symbols (char symbol, int cnt)
{
    while (cnt-- > 0)
        putchar (symbol);
}

void progress ()
{
    ++progress_count;
    if (progress_count % progress_step == 0) {
        putchar ('#');
        fflush (stdout);
    }
}

void quit (void)
{
    if (target != 0) {
        target_close (target);
        free (target);
        target = 0;
    }
}

void interrupted (int signum)
{
    fprintf (stderr, _("\nInterrupted.\n"));
    quit();
    _exit (-1);
}

void do_probe ()
{
    /* Open and detect the device. */
    atexit (quit);
    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }
    printf (_("Processor: %s (id %08X)\n"), target_cpu_name (target),
        target_idcode (target));
    printf (_("Main flash memory: %d kbytes\n"), target_main_flash_bytes (target) / 1024);
    printf (_("Info flash memory: %d kbytes\n"), target_info_flash_bytes (target) / 1024);
}

void program_block (target_t *mc, unsigned addr, int len, int info_flash)
{
    /* Write flash memory. */
    target_program_block (mc, memory_base + addr,
        (len + 3) / 4, (unsigned*) (memory_data + addr), info_flash);
}

void write_block (target_t *mc, unsigned addr, int len)
{
    /* Write static memory. */
    target_write_block (mc, memory_base + addr,
        (len + 3) / 4, (unsigned*) (memory_data + addr));
}

int verify_block (target_t *mc, unsigned addr, int len, int info_flash, int print_error)
{
    int i;
    unsigned word, expected, block [BLOCKSZ/4];

    target_read_block (mc, memory_base + addr, (len+3)/4, block, info_flash);

    for (i=0; i<len; i+=4) {
        expected = *(unsigned*) (memory_data + addr + i);
        word = block [i/4];
        if (debug_level > 1)
            printf (_("read word %08X at address %08X\n"),
                word, addr + i + memory_base);
        if (word != expected) {
            if (print_error)
                printf (_("\nerror at address %08X: file=%08X, mem=%08X\n"),
                    addr + i + memory_base, expected, word);
            return 0;
        }
    }
    return 1;
}

void do_program (char *filename, int info_flash)
{
    unsigned addr;
    int len;
    int progress_len;
    void *t0;
    int cur_len = 0;
    int i;
    unsigned nb_of_retries = 0;

    printf (_("Memory: %08X-%08X, total %d bytes\n"), memory_base,
        memory_base + memory_len, memory_len);

    /* Open and detect the device. */
    atexit (quit);
    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }
    printf (_("Processor: %s\n"), target_cpu_name (target));
    printf (_("Main flash memory: %d kbytes\n"), target_main_flash_bytes (target) / 1024);
    printf (_("Info flash memory: %d kbytes\n"), target_info_flash_bytes (target) / 1024);

    if (! verify_only) {
        /* Erase flash. */
        if (info_flash)
	    target_erase (target, memory_base, info_flash);
	else
	    while (cur_len < memory_len) {
	        target_erase_block (target, memory_base + cur_len);
	        cur_len += FLASH_BLOCK_SZ;
	    }
    }
    for (progress_step=1; ; progress_step<<=1) {
        progress_len = 1 + memory_len / progress_step / BLOCKSZ;
        if (progress_len < 64)
            break;
    }

    if (prog_then_verify) {
        progress_count = 0;
        t0 = fix_time ();
        if (! verify_only) {
	    printf (_("Program: "));
            print_symbols ('.', progress_len);
            print_symbols ('\b', progress_len);
            fflush (stdout);
            for (addr=0; (int)addr<memory_len; addr+=BLOCKSZ) {
                len = BLOCKSZ;
                if (memory_len - addr < len)
                    len = memory_len - addr;
                if (! verify_only)
                    program_block (target, addr, len, info_flash);
                progress ();
            }
            printf (_("# done\n"));
        }

        target_close (target);
        free (target);

        target = target_open (1);
        if (! target) {
            fprintf (stderr, _("Error detecting device -- check cable!\n"));
            exit (1);
        }

        printf (_("Verify:  "));
        print_symbols ('.', progress_len);
        print_symbols ('\b', progress_len);
        fflush (stdout);

        for (addr=0; (int)addr<memory_len; addr+=BLOCKSZ) {
            len = BLOCKSZ;
            if (memory_len - addr < len)
                len = memory_len - addr;
            progress ();
            if (! verify_block (target, addr, len, info_flash, 1))
                exit (0);
        }
        printf (_("# done\n"));
        printf (_("Rate: %ld bytes per second\n"),
            memory_len * 1000L / mseconds_elapsed (t0));
    } else {    // !prog_then_verify
        progress_count = 0;
        t0 = fix_time ();
        if (! verify_only) {
	    printf (_("Program and verify: "));
            print_symbols ('.', progress_len);
            print_symbols ('\b', progress_len);
            fflush (stdout);
            for (addr=0; (int)addr<memory_len; addr+=BLOCKSZ) {
                len = BLOCKSZ;
                if (memory_len - addr < len)
                    len = memory_len - addr;
                if (! verify_only)
                    program_block (target, addr, len, info_flash);
                for (i = 0; i < BLOCK_PROG_NB_RETRIES; ++i) {
                    if (verify_block (target, addr, len, info_flash, 0))
                        break;
                    if (debug_level > 0)
                        fprintf (stderr, "Verify failed for block at %08X, retry...\n", addr);
                    if (! verify_only) {
                        /*
                        target_close (target);
                        free (target);

                        target = target_open (1);
                        if (! target) {
                            fprintf (stderr, _("Error detecting device -- check cable!\n"));
                            exit (1);
                        }
                        */
                        target_erase_block (target, addr);
                        program_block (target, addr, len, info_flash);
                    }                    
                }
                nb_of_retries += i;
                if (i == BLOCK_PROG_NB_RETRIES) {
                    fprintf (stderr, "\nVerification failed!\n");
                    printf ("Nb of retries: %d\n", nb_of_retries);
                    exit(0);
                }
                progress ();
            }
            printf (_("# done\n"));
            printf (_("Rate: %ld bytes per second\n"),
                memory_len * 1000L / mseconds_elapsed (t0));
            printf ("Nb of retries: %d\n", nb_of_retries);
        }
    }
}

void do_write ()
{
    unsigned addr;
    int len;
    int progress_len;
    void *t0;

    printf (_("Memory: %08X-%08X, total %d bytes\n"), memory_base,
        memory_base + memory_len, memory_len);

    /* Open and detect the device. */
    atexit (quit);
    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }
    printf (_("Processor: %s\n"), target_cpu_name (target));

    for (progress_step=1; ; progress_step<<=1) {
        progress_len = 1 + memory_len / progress_step / BLOCKSZ;
        if (progress_len < 64)
            break;
    }

    progress_count = 0;
    t0 = fix_time ();
    if (! verify_only) {
	printf (_("Write:   "));
        print_symbols ('.', progress_len);
        print_symbols ('\b', progress_len);
        fflush (stdout);
        for (addr=0; (int)addr<memory_len; addr+=BLOCKSZ) {
            len = BLOCKSZ;
            if (memory_len - addr < len)
                len = memory_len - addr;
            if (! verify_only)
                write_block (target, addr, len);
            progress ();
        }
        printf (_("# done\n"));
    }

    target_close (target);
    free (target);

    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }

    printf (_("Verify:  "));
    print_symbols ('.', progress_len);
    print_symbols ('\b', progress_len);
    fflush (stdout);

    for (addr=0; (int)addr<memory_len; addr+=BLOCKSZ) {
        len = BLOCKSZ;
        if (memory_len - addr < len)
            len = memory_len - addr;
        progress ();
        if (! verify_block (target, addr, len, 0, 1))
            exit (0);
    }
    printf (_("# done\n"));
    printf (_("Rate: %ld bytes per second\n"),
        memory_len * 1000L / mseconds_elapsed (t0));
}

void do_read (char *filename, int info_flash)
{
    FILE *fd;
    unsigned len, addr, data [BLOCKSZ/4];
    void *t0;

    fd = fopen (filename, "wb");
    if (! fd) {
        perror (filename);
        exit (1);
    }
    printf (_("Memory: %08X-%08X, total %d bytes\n"), memory_base,
        memory_base + memory_len, memory_len);

    /* Open and detect the device. */
    atexit (quit);
    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }
    for (progress_step=1; ; progress_step<<=1) {
        len = 1 + memory_len / progress_step / BLOCKSZ;
        if (len < 64)
            break;
    }
    printf ("Read: " );
    print_symbols ('.', len);
    print_symbols ('\b', len);
    fflush (stdout);

    progress_count = 0;
    t0 = fix_time ();
    for (addr=0; (int)addr<memory_len; addr+=BLOCKSZ) {
        len = BLOCKSZ;
        if (memory_len - addr < len)
            len = memory_len - addr;
        progress ();

        target_read_block (target, memory_base + addr,
            (len + 3) / 4, data, info_flash);
        if (fwrite (data, 1, len, fd) != len) {
            fprintf (stderr, "%s: write error!\n", filename);
            exit (1);
        }
    }
    printf (_("# done\n"));
    printf (_("Rate: %ld bytes per second\n"),
        memory_len * 1000L / mseconds_elapsed (t0));
    fclose (fd);
}

void do_erase_block (unsigned addr)
{
    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }

    target_erase_block (target, addr);
}

void do_erase_all ()
{
    target = target_open (1);
    if (! target) {
        fprintf (stderr, _("Error detecting device -- check cable!\n"));
        exit (1);
    }

    target_erase (target, memory_base, 0);
}

/*
 * Print copying part of license
 */
static void gpl_show_copying (void)
{
    printf ("%s.\n\n", copyright);
    printf ("This program is free software; you can redistribute it and/or modify\n");
    printf ("it under the terms of the GNU General Public License as published by\n");
    printf ("the Free Software Foundation; either version 2 of the License, or\n");
    printf ("(at your option) any later version.\n");
    printf ("\n");
    printf ("This program is distributed in the hope that it will be useful,\n");
    printf ("but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
    printf ("MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n");
    printf ("GNU General Public License for more details.\n");
    printf ("\n");
}

/*
 * Print NO WARRANTY part of license
 */
static void gpl_show_warranty (void)
{
    printf ("%s.\n\n", copyright);
    printf ("BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, THERE IS NO WARRANTY\n");
    printf ("FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW.  EXCEPT WHEN\n");
    printf ("OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES\n");
    printf ("PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED\n");
    printf ("OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\n");
    printf ("MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS\n");
    printf ("TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.  SHOULD THE\n");
    printf ("PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY SERVICING,\n");
    printf ("REPAIR OR CORRECTION.\n");
    printf("\n");
    printf ("IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING\n");
    printf ("WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY AND/OR\n");
    printf ("REDISTRIBUTE THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES,\n");
    printf ("INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING\n");
    printf ("OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED\n");
    printf ("TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY\n");
    printf ("YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER\n");
    printf ("PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE\n");
    printf ("POSSIBILITY OF SUCH DAMAGES.\n");
    printf("\n");
}

int main (int argc, char **argv)
{
    int ch, read_mode = 0, memory_write_mode = 0, erase_mode = 0;
    int info_flash = 0;
    //unsigned erase_addr = 0;
    static const struct option long_options[] = {
        { "help",        0, 0, 'h' },
        { "warranty",    0, 0, 'W' },
        { "copying",     0, 0, 'C' },
        { "version",     0, 0, 'V' },
        { NULL,          0, 0, 0 },
    };

    /* Set locale and message catalogs. */
    setlocale (LC_ALL, "");
#if defined (__CYGWIN32__) || defined (MINGW32)
    /* Files with localized messages should be placed in
     * the current directory or in c:/Program Files/milprog. */
    if (access ("./ru/LC_MESSAGES/milprog.mo", R_OK) == 0)
        bindtextdomain ("milprog", ".");
    else
        bindtextdomain ("milprog", "c:/Program Files/milprog");
#else
    bindtextdomain ("milprog", "/usr/local/share/locale");
#endif
    textdomain ("milprog");

    setvbuf (stdout, (char *)NULL, _IOLBF, 0);
    setvbuf (stderr, (char *)NULL, _IOLBF, 0);
    printf (_("Programmer for Milandr ARM microcontrollers, Version %s\n"), VERSION);
    progname = argv[0];
    copyright = _("Copyright (C) 2010, 2011 Serge Vakulenko");
    signal (SIGINT, interrupted);
#ifdef __linux__
    signal (SIGHUP, interrupted);
#endif
    signal (SIGTERM, interrupted);

    while ((ch = getopt_long (argc, argv, "vDhrweaiCVW",
      long_options, 0)) != -1) {
        switch (ch) {
        case 'v':
            ++verify_only;
            continue;
        case 'D':
            ++debug_level;
            continue;
        case 'r':
            ++read_mode;
            continue;
        case 'w':
            ++memory_write_mode;
            continue;
        case 'e':
            ++erase_mode;
            //erase_addr = strtoul (optarg, 0, 0);
            continue;
        case 'a':
            ++prog_then_verify;
            continue;
        case 'i':
            ++info_flash;
            continue;
        case 'h':
            break;
        case 'V':
            /* Version already printed above. */
            return 0;
        case 'C':
            gpl_show_copying ();
            return 0;
        case 'W':
            gpl_show_warranty ();
            return 0;
        }
usage:
        printf ("%s.\n\n", copyright);
        printf ("MILprog comes with ABSOLUTELY NO WARRANTY; for details\n");
        printf ("use `--warranty' option. This is Open Source software. You are\n");
        printf ("welcome to redistribute it under certain conditions. Use the\n");
        printf ("'--copying' option for details.\n\n");
        printf ("Probe:\n");
        printf ("       milprog\n");
        printf ("\nWrite flash memory:\n");
        printf ("       milprog [-v] file.srec\n");
        printf ("       milprog [-v] file.hex\n");
        printf ("       milprog [-v] file.bin [address]\n");
        printf ("\nWrite static memory:\n");
        printf ("       milprog -w [-v] file.srec\n");
        printf ("       milprog -w [-v] file.hex\n");
        printf ("       milprog -w [-v] file.bin [address]\n");
        printf ("\nRead memory:\n");
        printf ("       milprog -r file.bin address length\n");
        printf ("\nArgs:\n");
        printf ("       file.srec           Code file in SREC format\n");
        printf ("       file.hex            Code file in HEX format\n");
        printf ("       file.bin            Code file in binary format\n");
        printf ("       address             Address of flash memory, default 0x%08X\n",
            DEFAULT_ADDR);
        printf ("       -v                  Verify only\n");
        printf ("       -w                  Memory write mode\n");
        printf ("       -r                  Read mode\n");
        printf ("       -e                  Erase all\n");
        printf ("       -a                  Verify only after full chip programming\n");
        printf ("       -i                  Use info flash instead of main\n");
        printf ("       -D                  Debug mode\n");
        printf ("       -h, --help          Print this help message\n");
        printf ("       -V, --version       Print version\n");
        printf ("       -C, --copying       Print copying information\n");
        printf ("       -W, --warranty      Print warranty information\n");
        printf ("\n");
        return 0;
    }
    printf ("%s.\n", copyright);
    argc -= optind;
    argv += optind;

    switch (argc) {
    case 0:
        if (erase_mode) {
            //do_erase_block (erase_addr);
            do_erase_all ();
            break;
        } else {
            do_probe ();
        }
        break;
    case 1:
        memory_len = read_srec (argv[0], memory_data);
        if (memory_len == 0) {
            memory_len = read_hex (argv[0], memory_data);
            if (memory_len == 0) {
                memory_base = DEFAULT_ADDR;
                memory_len = read_bin (argv[0], memory_data);
            }
        }
        if (memory_write_mode)
            do_write ();
        else
            do_program (argv[0], info_flash);
        break;
    case 2:
        memory_base = strtoul (argv[1], 0, 0);
        memory_len = read_bin (argv[0], memory_data);
        if (memory_write_mode)
            do_write ();
        else
            do_program (argv[0], info_flash);
        break;
    case 3:
        if (! read_mode)
            goto usage;
        memory_base = strtoul (argv[1], 0, 0);
        memory_len = strtoul (argv[2], 0, 0);
        do_read (argv[0], info_flash);
        break;
    default:
        goto usage;
    }
    quit ();
    return 0;
}
