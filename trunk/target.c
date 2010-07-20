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
    unsigned    cpuid;
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

unsigned target_read_word (target_t *t, unsigned address)
{
    unsigned value;

    t->adapter->mem_ap_write (t->adapter, MEM_AP_TAR, address);
    value = t->adapter->mem_ap_read (t->adapter, MEM_AP_DRW);
    if (debug_level > 1) {
        fprintf (stderr, "word read %08x from %08x\n",
            value, address);
    }
    return value;
}

/*
 * Запись слова в память.
 */
void target_write_word (target_t *t, unsigned address, unsigned data)
{
    if (debug_level > 1) {
        fprintf (stderr, _("word write %08x to %08x\n"), data, address);
    }
    t->adapter->mem_ap_write (t->adapter, MEM_AP_TAR, address);
    t->adapter->mem_ap_write (t->adapter, MEM_AP_DRW, data);
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
    unsigned idcode = t->adapter->get_idcode (t->adapter);
    if (debug_level)
        fprintf (stderr, "idcode %08X\n", idcode);

    /* Проверяем идентификатор ARM Debug Interface v5. */
    if (idcode != 0x4ba00477) {
        /* Device not detected. */
        if (idcode == 0xffffffff || idcode == 0)
            fprintf (stderr, _("No response from device -- check power is on!\n"));
        else
            fprintf (stderr, _("No response from device -- unknown idcode 0x%08X!\n"),
                idcode);
        t->adapter->close (t->adapter);
        exit (1);
    }

    /* Включение питания блока отладки, сброс залипающих ошибок. */
    t->adapter->dp_write (t->adapter, DP_CTRL_STAT,
	CSYSPWRUPREQ | CDBGPWRUPREQ | CORUNDETECT |
        SSTICKYORUN | SSTICKYCMP | SSTICKYERR);

    /* Проверка регистра MEM-AP IDR. */
    unsigned apid = t->adapter->mem_ap_read (t->adapter, MEM_AP_IDR);
    if (apid != 0x24770011) {
        fprintf (stderr, _("Unknown type of memory access port, IDR=%08x.\n"),
                apid);
        t->adapter->close (t->adapter);
        exit (1);
    }

    /* Проверка регистра MEM-AP CFG. */
    unsigned cfg = t->adapter->mem_ap_read (t->adapter, MEM_AP_CFG);
    if (cfg & CFG_BIGENDIAN) {
        fprintf (stderr, _("Big endian memory type not supported, CFG=%08x.\n"),
                cfg);
        t->adapter->close (t->adapter);
        exit (1);
    }

    /* Установка режимов блока MEM-AP: регистр CSW. */
    t->adapter->mem_ap_write (t->adapter, MEM_AP_CSW, CSW_MASTER_DEBUG | CSW_HPROT |
        CSW_32BIT | CSW_ADDRINC_SINGLE);
    if (debug_level) {
        unsigned csw = t->adapter->mem_ap_read (t->adapter, MEM_AP_CSW);
        fprintf (stderr, "MEM-AP CSW = %08x\n", csw);
    }

    /* Останавливаем процессор. */
    unsigned dhcsr = target_read_word (t, DCB_DHCSR) & 0xFFFF;
    dhcsr |= DBGKEY | C_DEBUGEN | C_HALT;
    target_write_word (t, DCB_DHCSR, dhcsr);

    /* Проверяем идентификатор процессора. */
    t->cpuid = target_read_word (t, CPUID);
    switch (t->cpuid) {
    case 0x412fc230:    /* Миландр 1986ВМ91Т */
        t->cpu_name = "Cortex M3";
        t->flash_addr = 0x08000000;
        t->flash_bytes = 128*1024;
        break;
    default:
        /* Device not detected. */
        fprintf (stderr, _("Unknown CPUID=%08x.\n"), t->cpuid);
        t->adapter->close (t->adapter);
        exit (1);
    }
    t->is_running = 1;
    return t;
}

/*
 * Close the device.
 */
void target_close (target_t *t)
{
    /* TODO: resume execution */
    t->adapter->close (t->adapter);
}

const char *target_cpu_name (target_t *t)
{
    return t->cpu_name;
}

unsigned target_idcode (target_t *t)
{
    return t->cpuid;
}

unsigned target_flash_bytes (target_t *t)
{
    return t->flash_bytes;
}

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

/*
 * Чтение данных из памяти.
 */
void target_read_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

//fprintf (stderr, "target_read_block (addr = %x, nwords = %d)\n", addr, nwords);
    for (i=0; i<nwords; i++, addr+=4, data++) {
        t->adapter->mem_ap_write (t->adapter, MEM_AP_TAR, addr);
        *data = t->adapter->mem_ap_read (t->adapter, MEM_AP_DRW);
        if (debug_level)
            fprintf (stderr, _("block read %08x from %08x\n"), *data, addr);
    }
//fprintf (stderr, "    done (addr = %x)\n", addr);
}

#if 0
void target_write_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

    target_write_word (t, addr, *data++);
    for (i=1; i<nwords; i++)
        target_write_next (t, addr += 4, *data++);
}

static void target_program_block (target_t *t, unsigned addr,
    unsigned base, unsigned nwords, unsigned *data)
{
    while (nwords-- > 0) {
        target_write_nwords (t, 4,
            base + t->flash_addr_odd, t->flash_cmd_aa,
            base + t->flash_addr_even, t->flash_cmd_55,
            base + t->flash_addr_odd, t->flash_cmd_a0,
            addr, *data++);
        addr += 4;
    }
}
#endif
