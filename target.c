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
    if (debug_level) {
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
    if (debug_level) {
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

    /* Выбираем 3-й блок регистров MEM-AP. */
    t->adapter->dp_write (t->adapter, DP_SELECT, MEM_AP_IDR & 0xF0);

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

    /* Выбираем 0-й блок регистров MEM-AP. */
    t->adapter->dp_write (t->adapter, DP_SELECT, MEM_AP_CSW & 0xF0);

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

    /* Подача тактовой частоты на периферийные блоки. */
    target_write_word (t, PER_CLOCK, 0xFFFFFFFF);

    /* Запрет прерываний. */
    target_write_word (t, ICER0, 0xFFFFFFFF);
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
    printf (_("Erase: %08X..."), t->flash_addr);
    fflush (stdout);
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);          // set CON
    target_write_word (t, EEPROM_KEY, 0x8AAA5551);
    target_write_word (t, EEPROM_DI, 0);
    for (addr=0; addr<16; addr+=4) {
	target_write_word (t, EEPROM_ADR, addr);
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_WR);       // set WR
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);      // clear WR
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_MAS1 |     // set MAS1
                                          EEPROM_CMD_XE |       // set XE
                                          EEPROM_CMD_ERASE);    // set ERASE
	mdelay (1);                                             // 5 us
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_MAS1 |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_ERASE |
                                          EEPROM_CMD_NVSTR);    // set NVSTR
	mdelay (40);                                            // 40 ms
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_MAS1 |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_NVSTR);    // clear ERASE
	mdelay (1);                                             // 100 us
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);      // clear XE, NVSTR, MAS1
	mdelay (1);                                             // 1 us
    }
    target_write_word (t, EEPROM_CMD, 0);                       // clear CON
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
    t->adapter->mem_ap_write (t->adapter, MEM_AP_TAR, addr);
    for (i=0; i<nwords; i++, addr+=4, data++) {
        *data = t->adapter->mem_ap_read (t->adapter, MEM_AP_DRW);
        if (debug_level)
            fprintf (stderr, _("block read %08x from %08x\n"), *data, addr);
    }
//fprintf (stderr, "    done (addr = %x)\n", addr);
}

void target_write_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

    for (i=0; i<nwords; i++, addr+=4)
        target_write_word (t, addr, *data++);
}

/*
 * Программирование одной страницы памяти (до 256 слов).
 * Страница должна быть предварительно стёрта.
 */
void target_program_block (target_t *t, unsigned pageaddr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

    target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);		// set CON
    target_write_word (t, EEPROM_KEY, 0x8AAA5551);
    for (i=0; i<nwords; i++) {
        target_write_word (t, EEPROM_ADR, pageaddr + i*4);
	mdelay (1);                                             // 10 us
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |	// set XE
                                          EEPROM_CMD_PROG);     // set PROG
	mdelay (1);                                             // 5 us
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR);	// set NVSTR
	target_write_word (t, EEPROM_DI, data [i]);
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR |
                                          EEPROM_CMD_WR);       // set WR
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR);	// clear WR
	mdelay (1); 					        // 10 us
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR |
                                          EEPROM_CMD_YE);	// set YE
	mdelay (1);                                             // 40 us
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR);	// clear YE
	target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_NVSTR);	// clear PROG
	mdelay (1);                                             // 5 us
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);	// clear XE, NVSTR
	mdelay (1);                                             // 5 us
    }
    target_write_word (t, EEPROM_CMD, 0);                       // clear CON
}
