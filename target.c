/*
 * Интерфейс через JTAG к процессору Элвис Мультикор.
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
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "target.h"
#include "adapter.h"
#include "arm-jtag.h"
#include "localize.h"

struct _target_t {
    adapter_t   *adapter;
    const char  *cpu_name;
    unsigned    idcode;
    unsigned    is_running;
    unsigned    flash_addr;
    unsigned    flash_bytes;
};

#if defined (__CYGWIN32__) || defined (MINGW32)
/*
 * Задержка в миллисекундах: Windows.
 */
#include <windows.h>

void mdelay (unsigned msec)
{
    Sleep (msec);
}
#else
/*
 * Задержка в миллисекундах: Unix.
 */
void mdelay (unsigned msec)
{
    usleep (msec * 1000);
}
#endif

#if 0
/*
 * Запись слова в память.
 */
void target_write_next (target_t *t, unsigned phys_addr, unsigned data)
{
    unsigned count;

    if (debug_level)
        fprintf (stderr, _("write %08x to %08x\n"), data, phys_addr);

    t->adapter->memap_write (t->adapter, phys_addr, OnCD_OMAR);
    t->adapter->memap_write (t->adapter, data, OnCD_OMDR);
    t->adapter->memap_write (t->adapter, 0, OnCD_MEM);

    if (t->is_running) {
        /* Если процессор запущен, обращение к памяти произойдёт не сразу.
         * Надо ждать появления бита RDYm в регистре OSCR. */
        for (count = 100; count != 0; count--) {
            t->adapter->oscr = t->adapter->memap_read (t->adapter, OnCD_OSCR);
            if (t->adapter->oscr & OSCR_RDYm)
                break;
            mdelay (1);
        }
        if (count == 0) {
            fprintf (stderr, _("Timeout writing memory, aborted. OSCR=%#x\n"),
                t->adapter->oscr);
            exit (1);
        }
    }
}

void target_write_word (target_t *t, unsigned phys_addr, unsigned data)
{
    if (debug_level)
        fprintf (stderr, _("write word %08x to %08x\n"), data, phys_addr);

    /* Allow memory access */
    unsigned oscr_new = (t->adapter->oscr & ~OSCR_RO) | OSCR_SlctMEM;
    if (oscr_new != t->adapter->oscr) {
        t->adapter->oscr = oscr_new;
        t->adapter->memap_write (t->adapter, t->adapter->oscr, OnCD_OSCR);
    }

    target_write_next (t, phys_addr, data);
}

/*
 * Чтение слова из памяти.
 */
void target_read_start (target_t *t)
{
    /* Allow memory access */
    unsigned oscr_new = t->adapter->oscr | OSCR_SlctMEM | OSCR_RO;
    if (oscr_new != t->adapter->oscr) {
        t->adapter->oscr = oscr_new;
        t->adapter->memap_write (t->adapter, t->adapter->oscr, OnCD_OSCR);
    }
}

unsigned target_read_next (target_t *t, unsigned phys_addr)
{
    unsigned count, data;

    t->adapter->memap_write (t->adapter, phys_addr, OnCD_OMAR);
    t->adapter->memap_write (t->adapter, 0, OnCD_MEM);

/* Адаптер Elvees USB-JTAG не работает, если не делать проверку RDYm. */
//    if (t->is_running) {
        /* Если процессор запущен, обращение к памяти произойдёт не сразу.
         * Надо ждать появления бита RDYm в регистре OSCR. */
        for (count = 100; count != 0; count--) {
            t->adapter->oscr = t->adapter->memap_read (t->adapter, OnCD_OSCR);
            if (t->adapter->oscr & OSCR_RDYm)
                break;
            mdelay (1);
        }
        if (count == 0) {
            fprintf (stderr, _("Timeout reading memory, aborted. OSCR=%#x\n"),
                t->adapter->oscr);
            exit (1);
        }
//    }
    data = t->adapter->memap_read (t->adapter, OnCD_OMDR);

    if (debug_level)
        fprintf (stderr, _("read %08x from     %08x\n"), data, phys_addr);
    return data;
}

void target_write_nwords (target_t *t, unsigned nwords, ...)
{
    va_list args;
    unsigned addr, data, i;

    va_start (args, nwords);
    if (t->adapter->write_nwords) {
        t->adapter->write_nwords (t->adapter, nwords, args);
        va_end (args);
        return;
    }
    addr = va_arg (args, unsigned);
    data = va_arg (args, unsigned);
    target_write_word (t, addr, data);
    for (i=1; i<nwords; i++) {
        addr = va_arg (args, unsigned);
        data = va_arg (args, unsigned);
        target_write_next (t, addr, data);
    }
    va_end (args);
}
#endif

unsigned target_read_word (target_t *t, unsigned address)
{
    unsigned value;

    t->adapter->memap_write (t->adapter, MEM_AP_TAR, address & 0xFFFFFFF0);
    value = t->adapter->memap_read (t->adapter, MEM_AP_DRW | (address & 0xC));
    if (debug_level > 1) {
        fprintf (stderr, "target read %08x from %08x\n",
            value, address);
    }
    return value;
}

/*
 * Устанавливаем соединение с адаптером JTAG.
 * Не надо сбрасывать процессор!
 * Программа должна продолжать выполнение.
 */
target_t *target_open (int need_reset)
{
    target_t *t;

    t = calloc (1, sizeof (target_t));
    if (! t) {
        fprintf (stderr, _("Out of memory\n"));
        exit (-1);
    }
    t->cpu_name = "Unknown";

    /* Ищем адаптер JTAG: MPSSE. */
    t->adapter = adapter_open_mpsse ();
    if (! t->adapter) {
        fprintf (stderr, _("No JTAG adapter found.\n"));
        exit (-1);
    }

    /* Проверяем идентификатор процессора. */
    t->idcode = t->adapter->get_idcode (t->adapter);
    if (debug_level)
        fprintf (stderr, "idcode %08X\n", t->idcode);

    /* Проверяем идентификатор ARM Cortex M3. */
    if (t->idcode != 0x4ba00477) {
        /* Device not detected. */
        if (t->idcode == 0xffffffff || t->idcode == 0)
            fprintf (stderr, _("No response from device -- check power is on!\n"));
        else
            fprintf (stderr, _("No response from device -- unknown idcode 0x%08X!\n"),
                t->idcode);
        t->adapter->close (t->adapter);
        exit (1);
    }

    /* Включение питания блока отладки, сброс залипающих ошибок. */
    t->adapter->dp_write (t->adapter, DP_CTRL_STAT,
	CSYSPWRUPREQ | CDBGPWRUPREQ | CORUNDETECT |
        SSTICKYORUN | SSTICKYCMP | SSTICKYERR);

    /* Проверка регистра MEM-AP IDR. */
    unsigned apid = t->adapter->memap_read (t->adapter, MEM_AP_IDR);
    if (apid != 0x24770011) {
        /* Device not detected. */
        fprintf (stderr, _("Unknown type of memory access port, IDR=%08x.\n"),
                apid);
        t->adapter->close (t->adapter);
        exit (1);
    }

    /* Проверка регистра MEM-AP CFG. */
    unsigned cfg = t->adapter->memap_read (t->adapter, MEM_AP_CFG);
    if (cfg & CFG_BIGENDIAN) {
        /* Device not detected. */
        fprintf (stderr, _("Big endian memory type not supported, CFG=%08x.\n"),
                cfg);
        t->adapter->close (t->adapter);
        exit (1);
    }

    /* Установка режимов блока MEM-AP: регистр CSW. */
    t->adapter->memap_write (t->adapter, MEM_AP_CSW, CSW_MASTER_DEBUG | CSW_HPROT |
        CSW_32BIT | CSW_ADDRINC_SINGLE);
    if (debug_level) {
        unsigned csw = t->adapter->memap_read (t->adapter, MEM_AP_CSW);
        fprintf (stderr, "MEM-AP CSW = %08x\n", csw);
    }

    t->cpu_name = "Cortex M3";
    t->flash_addr = 0x08000000;
    t->flash_bytes = 128*1024;
    t->is_running = 1;
    return t;
}

/*
 * Close the device.
 */
void target_close (target_t *t)
{
    if (! t->is_running)
        target_resume (t);
    t->adapter->close (t->adapter);
}

const char *target_cpu_name (target_t *t)
{
    return t->cpu_name;
}

unsigned target_idcode (target_t *t)
{
    return t->idcode;
}

unsigned target_flash_bytes (target_t *t)
{
    return t->flash_bytes;
}

#if 0
/*
 * Стирание всей flash-памяти.
 */
int target_erase (target_t *t, unsigned addr)
{
    printf (_("Erase: %08X"), t->flash_addr);

    /*TODO*/

    for (;;) {
        fflush (stdout);
        mdelay (250);
        unsigned word = target_read_word (t, t->flash_addr);
        if (word == 0xffffffff)
            break;
        printf (".");
    }
    mdelay (250);
    printf (_(" done\n"));
    return 1;
}

void target_read_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

//fprintf (stderr, "target_read_block (addr = %x, nwords = %d)\n", addr, nwords);
    if (t->adapter->read_block) {
        while (nwords > 0) {
            unsigned n = nwords;
            if (n > t->adapter->block_words)
                n = t->adapter->block_words;
            t->adapter->read_block (t->adapter, n, addr, data);
            data += n;
            addr += n*4;
            nwords -= n;
        }
        return;
    }
    target_read_start (t);
    for (i=0; i<nwords; i++, addr+=4)
        *data++ = target_read_next (t, addr);
//fprintf (stderr, "    done (addr = %x)\n", addr);
}

void target_write_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

    if (t->adapter->write_block) {
        while (nwords > 0) {
            unsigned n = nwords;
            if (n > t->adapter->block_words)
                n = t->adapter->block_words;
            t->adapter->write_block (t->adapter, n, addr, data);
            data += n;
            addr += n*4;
            nwords -= n;
        }
        return;
    }
    target_write_word (t, addr, *data++);
    for (i=1; i<nwords; i++)
        target_write_next (t, addr += 4, *data++);
}

static void target_program_block32 (target_t *t, unsigned addr,
    unsigned base, unsigned nwords, unsigned *data)
{
    if (t->adapter->program_block32) {
        while (nwords > 0) {
            unsigned n = nwords;
            if (n > t->adapter->program_block_words)
                n = t->adapter->program_block_words;
            t->adapter->program_block32 (t->adapter,
                n, base, addr, data,
                t->flash_addr_odd, t->flash_addr_even,
                t->flash_cmd_aa, t->flash_cmd_55, t->flash_cmd_a0);
            data += n;
            addr += n*4;
            nwords -= n;
        }
        return;
    }
    while (nwords-- > 0) {
        target_write_nwords (t, 4,
            base + t->flash_addr_odd, t->flash_cmd_aa,
            base + t->flash_addr_even, t->flash_cmd_55,
            base + t->flash_addr_odd, t->flash_cmd_a0,
            addr, *data++);
        addr += 4;
    }
}

static void target_program_block32_atmel (target_t *t, unsigned addr,
    unsigned base, unsigned nwords, unsigned *data)
{
    if (t->adapter->program_block32_protect) {
        while (nwords > 0) {
            t->adapter->program_block32_unprotect (t->adapter,
                128, base, addr, data,
                t->flash_addr_odd, t->flash_addr_even,
                t->flash_cmd_aa, t->flash_cmd_55, t->flash_cmd_a0);
            t->adapter->program_block32_protect (t->adapter,
                128, base, addr, data,
                t->flash_addr_odd, t->flash_addr_even,
                t->flash_cmd_aa, t->flash_cmd_55, t->flash_cmd_a0);
            if (nwords <= 128)
                break;
            data += 128;
            addr += 128*4;
            nwords -= 128;
        }
        return;
    }
    while (nwords > 0) {
        /* Unprotect. */
        target_write_nwords (t, 6,
            base + t->flash_addr_odd, t->flash_cmd_aa,
            base + t->flash_addr_even, t->flash_cmd_55,
            base + t->flash_addr_odd, 0x80808080,
            base + t->flash_addr_odd, t->flash_cmd_aa,
            base + t->flash_addr_even, t->flash_cmd_55,
            base + t->flash_addr_odd, 0x20202020);
        target_write_block (t, addr, 128, data);

        /* Protect. */
        target_write_nwords (t, 3,
            base + t->flash_addr_odd, t->flash_cmd_aa,
            base + t->flash_addr_even, t->flash_cmd_55,
            base + t->flash_addr_odd, t->flash_cmd_a0);
        target_write_block (t, addr, 128, data);

        data += 128;
        addr += 128*4;
        nwords -= 128;
    }
}

static void target_program_block64 (target_t *t, unsigned addr,
    unsigned base, unsigned nwords, unsigned *data)
{
    if (t->adapter->program_block64) {
        while (nwords > 0) {
            unsigned n = nwords;
            if (n > t->adapter->program_block_words)
                n = t->adapter->program_block_words;
            t->adapter->program_block64 (t->adapter,
                n, base, addr, data,
                t->flash_addr_odd, t->flash_addr_even,
                t->flash_cmd_aa, t->flash_cmd_55, t->flash_cmd_a0);
            data += n;
            addr += n*4;
            nwords -= n;
        }
        return;
    }
    if (addr & 4) {
        /* Старшая половина 64-разрядной шины. */
        base += 4;
    }
    while (nwords-- > 0) {
        target_write_nwords (t, 4,
            base + t->flash_addr_odd, t->flash_cmd_aa,
            base + t->flash_addr_even, t->flash_cmd_55,
            base + t->flash_addr_odd, t->flash_cmd_a0,
            addr, *data++);
        addr += 4;
    }
}

void target_program_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
//fprintf (stderr, "target_program_block (addr = %x, nwords = %d), flash_width = %d, base = %x\n", addr, nwords, t->flash_width, t->flash_addr);
    target_program_block32 (t, addr, nwords, data);
}
#endif

void target_stop (target_t *t)
{
    if (! t->is_running)
        return;
    t->adapter->stop_cpu (t->adapter);
    t->is_running = 0;

//    unsigned cfg = t->adapter->memap_read (t->adapter, MEM_AP_CFG);
//    unsigned base = t->adapter->memap_read (t->adapter, MEM_AP_BASE);
//    unsigned idr = t->adapter->memap_read (t->adapter, MEM_AP_IDR);
//    printf ("IDR = %08X, BASE = %08X, CFG = %08X\n", idr, base, cfg);
}

void target_step (target_t *t)
{
#if 0
    if (t->is_running)
        return;
    target_restore_state (t);
    if (t->adapter->step_cpu)
        t->adapter->step_cpu (t->adapter);
    else {
        t->adapter->memap_write (t->adapter,
            0, OnCD_GO | IRd_FLUSH_PIPE | IRd_STEP_1CLK);
    }
    target_save_state (t);
#endif
fprintf (stderr, "target_step() not implemented yet\n");
}

void target_resume (target_t *t)
{
#if 0
    if (t->is_running)
        return;
    target_restore_state (t);
    t->is_running = 1;
    if (t->adapter->run_cpu)
        t->adapter->run_cpu (t->adapter);
    else {
        t->adapter->memap_write (t->adapter,
            0, OnCD_GO | IRd_RESUME | IRd_FLUSH_PIPE);
    }
#endif
fprintf (stderr, "target_resume() not implemented yet\n");
}

void target_run (target_t *t, unsigned addr)
{
#if 0
    if (t->is_running)
        return;

    /* Изменение адреса следующей команды реализуется
     * аналогично входу в отработчик исключения. */
    t->exception = addr;

    target_restore_state (t);
    t->is_running = 1;
    if (t->adapter->run_cpu)
        t->adapter->run_cpu (t->adapter);
    else {
        t->adapter->memap_write (t->adapter,
            0, OnCD_GO | IRd_RESUME | IRd_FLUSH_PIPE);
    }
#endif
}

#if 0
void target_restart (target_t *t)
{
    if (! t->is_running)
        target_restore_state (t);
    t->adapter->reset_cpu (t->adapter);
    t->is_running = 1;
}
#endif
