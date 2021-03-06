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
    unsigned    main_flash_addr;
    unsigned    main_flash_bytes;
    unsigned    info_flash_bytes;
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
 */
target_t *target_open (int need_reset)
{
    target_t *t;
    unsigned idcode;
    unsigned dhcsr;

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
    idcode = t->adapter->get_idcode (t->adapter);
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
	//t->adapter->reset_cpu (t->adapter);
	
	mdelay (1);

    /* Включение питания блока отладки, сброс залипающих ошибок. */
	unsigned ctl = CSYSPWRUPREQ | CDBGPWRUPREQ | SSTICKYCMP | SSTICKYERR;
	unsigned ack;

    do {
        t->adapter->dp_write (t->adapter, DP_CTRL_STAT, ctl);
        ack = t->adapter->dp_read (t->adapter, DP_CTRL_STAT);
    } while (! (ack & (CDBGPWRUPACK | CSYSPWRUPACK)));
    
    unsigned apid;
    do {
        t->adapter->dp_write (t->adapter, DP_SELECT, MEM_AP_IDR & 0xF0);
        apid = t->adapter->mem_ap_read (t->adapter, MEM_AP_IDR);
    } while (apid == 0);
    
    if (debug_level) 
    	fprintf (stderr, "apid = %08X\n", apid);
    if (apid != 0x24770011 && apid != 0x44770001) {
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
    t->adapter->mem_ap_write (t->adapter, MEM_AP_CSW, CSW_HPROT | CSW_32BIT | CSW_ADDRINC_SINGLE);
    if (debug_level) {
        unsigned csw = t->adapter->mem_ap_read (t->adapter, MEM_AP_CSW);
        fprintf (stderr, "MEM-AP CSW = %08x\n", csw);
    }
    
    /* Останавливаем процессор. */
    unsigned retry;
    for (retry=1; ; ++retry) {

		//if (retry % 5 == 0) t->adapter->reset_cpu (t->adapter);

        target_write_word (t, DCB_DHCSR, DBGKEY | C_DEBUGEN | C_HALT | C_MASKINTS);
        dhcsr = target_read_word (t, DCB_DHCSR) & 0xFFFFFF;
        //fprintf (stderr, "DP_CTRL_STAT: %08X\n", t->adapter->dp_read (t->adapter, DP_CTRL_STAT));    
        if (dhcsr == (C_DEBUGEN | C_HALT | C_MASKINTS | S_REGRDY | S_HALT)) {
            break; /* Процессор остановлен */
        }
        
        if (retry > 200) {
            fprintf (stderr, "Cannot write to DHCSR, aborted\n");
            t->adapter->mem_ap_write (t->adapter, MEM_AP_CSW, 0);
            t->adapter->close (t->adapter);
            exit (1);
        }
        
        /* Сброс залипающих флагов */
        t->adapter->dp_write (t->adapter, DP_CTRL_STAT, ctl);
    }

    /* Проверяем идентификатор процессора. */
    t->cpuid = target_read_word (t, CPUID);
    switch (t->cpuid) {
    case 0x411CC210:    /* Миландp 1986ВЕ1T */ 
        t->cpu_name = _("Milandr 1986BE1T");
        t->main_flash_addr = 0x00000000;
        t->main_flash_bytes = 128*1024;
        t->info_flash_bytes = 4*1024;
        break;
    case 0x412FC230:    /* Миландр 1986ВМ91Т */
        t->cpu_name = _("Milandr 1986BM91T");
        t->main_flash_addr = 0x08000000;
        t->main_flash_bytes = 128*1024;
        t->info_flash_bytes = 4*1024;
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
    return t;
}

/*
 * Close the device.
 */
void target_close (target_t *t)
{
    t->adapter->reset_cpu (t->adapter);

    /* Пускаем процессор. */
    target_write_word (t, DCB_DHCSR, DBGKEY);
    mdelay (1); /* Эта задержка нужна для ARM-USB-TINY-H */
    t->adapter->dp_read (t->adapter, DP_CTRL_STAT);
    t->adapter->mem_ap_write (t->adapter, MEM_AP_CSW, 0);
    t->adapter->dp_read (t->adapter, DP_CTRL_STAT);

    t->adapter->reset_cpu (t->adapter);
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

unsigned target_main_flash_addr(target_t *t)
{
    return t->main_flash_addr;
}

unsigned target_main_flash_bytes (target_t *t)
{
    return t->main_flash_bytes;
}

unsigned target_info_flash_bytes (target_t *t)
{
    return t->info_flash_bytes;
}


/*
 * На образцах 1986ВЕ91Т с маркировкой "1030" после прошивки
 * каждого блока чтение первых 4-х слов даёт FFFFFFFF.
 * Вероятно, мешает буфер кэш-памяти.
 * Следующий цикл устраняет этот эффект.
 */
static void clear_cache (target_t *t, unsigned addr)
{
    unsigned i;

    t->adapter->mem_ap_write (t->adapter, MEM_AP_TAR, addr);
    for (i=0; i<9; i++) {
        t->adapter->mem_ap_read (t->adapter, MEM_AP_DRW);
    }
}

/*
 * Стирание всей flash-памяти.
 */
int target_erase (target_t *t, unsigned addr, int info_flash)
{
    unsigned i;
    unsigned con = EEPROM_CMD_CON;
    if (info_flash) con |= EEPROM_CMD_IFREN;

    if (info_flash) printf (_("Erase: info flash..."));
    else            printf (_("Erase: %08X..."), t->main_flash_addr);
    fflush (stdout);
    target_write_word (t, EEPROM_KEY, 0x8AAA5551);		//enable access to EEPROM registers
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);  // set CON
    target_write_word (t, EEPROM_DI, ~0);

    for (i=0; i<16; i+=4) {
	target_write_word (t, EEPROM_ADR, i);
	target_write_word (t, EEPROM_CMD, con);
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_WR);       // set WR
	target_write_word (t, EEPROM_CMD, con); 	     	// clear WR
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_MAS1 |     // set MAS1
                                          EEPROM_CMD_XE |       // set XE
                                          EEPROM_CMD_ERASE);    // set ERASE
	// mdelay (1);                                             	// 5 us
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_MAS1 |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_ERASE |
                                          EEPROM_CMD_NVSTR);    // set NVSTR
	mdelay (40);                                            // 40 ms
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_MAS1 |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_NVSTR);    // clear ERASE
	// mdelay (1);                                             	// 100 us
	target_write_word (t, EEPROM_CMD, con);			// clear XE, NVSTR, MAS1
	// mdelay (1);                                             	// 1 us
    }
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_DELAY_4);      // clear CON
    clear_cache (t, addr);
    printf (_(" done\n"));
    return 1;
}

/*
 * Стирание одного блока памяти
 */
int target_erase_block (target_t *t, unsigned addr)
{
    unsigned i;

    //printf (_("Erase block: %08X..."), addr);
    //fflush (stdout);
    // next 2 lines were swapped - S.I
    target_write_word (t, EEPROM_KEY, 0x8AAA5551); // enable access to EEPROM registers
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);      // set CON
    target_write_word (t, EEPROM_DI, ~0);
    for (i=0; i<16; i+=4) {
        target_write_word (t, EEPROM_ADR, addr + i);
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_WR);       // set WR
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);      // clear WR
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |       // set XE
                                          EEPROM_CMD_ERASE);    // set ERASE
        mdelay (1);                                             // 5 us
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_ERASE |
                                          EEPROM_CMD_NVSTR);    // set NVSTR
        mdelay (50);                                            // 40 ms
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_NVSTR);    // clear ERASE
        mdelay (1);                                             // 5 us
        target_write_word (t, EEPROM_CMD, EEPROM_CMD_CON);      // clear XE, NVSTR
        mdelay (1);                                             // 1 us
    }
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_DELAY_4);      // clear CON
    clear_cache (t, addr);
    //printf (_(" done\n"));
    return 1;
}

/*
 * Чтение данных из памяти.
 */
void target_read_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data, int info_flash)
{
//fprintf (stderr, "target_read_block (addr = %x, nwords = %d)\n", addr, nwords);
    unsigned con = EEPROM_CMD_CON;
    if (info_flash) {
        con |= EEPROM_CMD_IFREN;
    }
    
    target_write_word (t, EEPROM_KEY, 0x8AAA5551); // enable access to EEPROM registers
    target_write_word (t, EEPROM_CMD, con);

	int i;
    for (i=0; i<nwords; i++) {
        target_write_word (t, EEPROM_ADR, addr + i*4);
        target_write_word (t, EEPROM_CMD, con | EEPROM_CMD_XE | 
                                          EEPROM_CMD_YE | EEPROM_CMD_SE);
        data [i] = target_read_word (t, EEPROM_DO);
        target_write_word (t, EEPROM_CMD, con);
    }
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_DELAY_4);          // clear CON
}

void target_write_block (target_t *t, unsigned addr,
    unsigned nwords, unsigned *data)
{
    unsigned i;

    t->adapter->mem_ap_write (t->adapter, MEM_AP_TAR, addr);
    for (i=0; i<nwords; i++, addr+=4, data++) {
        if (debug_level) {
            fprintf (stderr, _("block write %08x to %08x\n"), *data, addr);
        }
        t->adapter->mem_ap_write (t->adapter, MEM_AP_DRW, *data);
    }
}

/*
 * Программирование одной страницы памяти (до 256 слов).
 * Страница должна быть предварительно стёрта.
 */
void target_program_block (target_t *t, unsigned pageaddr,
    unsigned nwords, unsigned *data, int info_flash)
{
    unsigned i;
    unsigned con = EEPROM_CMD_CON;
    if (info_flash) {
       con |= EEPROM_CMD_IFREN;
    }

    target_write_word (t, EEPROM_KEY, 0x8AAA5551);			// enable register access to EEPROM regs
    target_write_word (t, EEPROM_CMD, con);		// set CON

    for (i=0; i<nwords; i++) {
        target_write_word (t, EEPROM_ADR, pageaddr + i*4);
        //mdelay (1);
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |	// set XE
                                          EEPROM_CMD_PROG); // set PROG
	//mdelay (1);
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR);// set NVSTR
	target_write_word (t, EEPROM_DI, data [i]);
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR |
                                          EEPROM_CMD_WR);   // set WR
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR);// clear WR
	//mdelay (1);
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR |
                                          EEPROM_CMD_YE);	// set YE
	//mdelay (1);
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_PROG |
                                          EEPROM_CMD_NVSTR);// clear YE
	target_write_word (t, EEPROM_CMD, con |
                                          EEPROM_CMD_XE |
                                          EEPROM_CMD_NVSTR);// clear PROG
	//mdelay (1);
        target_write_word (t, EEPROM_CMD, con);	// clear XE, NVSTR
	//mdelay (1);
    }
    target_write_word (t, EEPROM_CMD, EEPROM_CMD_DELAY_4); // clear CON

    clear_cache (t, pageaddr);
}
