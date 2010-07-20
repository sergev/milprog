/*
 * Обобщённый JTAG-адаптер. Программный интерфейс нижнего уровня.
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
#include <stdarg.h>

typedef struct _adapter_t adapter_t;

struct _adapter_t {
    const char *name;

    /* Регистр управления блоком отладки. */
    unsigned oscr;

    /*
     * Обязательные функции.
     */
    void (*close) (adapter_t *a);
    unsigned (*get_idcode) (adapter_t *a);
    void (*reset_cpu) (adapter_t *a);
    void (*dp_write) (adapter_t *a, int reg, unsigned val);
    unsigned (*dp_read) (adapter_t *a, int reg);
    void (*memap_write) (adapter_t *a, int reg, unsigned val);
    unsigned (*memap_read) (adapter_t *a, int reg);
#if 0
    /*
     * Расширенные возможности.
     */
    unsigned block_words;
    unsigned program_block_words;

    void (*step_cpu) (adapter_t *a);
    void (*run_cpu) (adapter_t *a);
    void (*read_block) (adapter_t *adapter,
        unsigned nwords, unsigned addr, unsigned *data);
    void (*write_block) (adapter_t *adapter,
        unsigned nwords, unsigned addr, unsigned *data);
    void (*write_nwords) (adapter_t *adapter, unsigned nwords, va_list args);
    void (*program_block32) (adapter_t *adapter,
        unsigned nwords, unsigned base, unsigned addr, unsigned *data,
        unsigned addr_odd, unsigned addr_even,
        unsigned cmd_aa, unsigned cmd_55, unsigned cmd_a0);
    void (*program_block32_unprotect) (adapter_t *adapter,
        unsigned nwords, unsigned base, unsigned addr, unsigned *data,
        unsigned addr_odd, unsigned addr_even,
        unsigned cmd_aa, unsigned cmd_55, unsigned cmd_a0);
    void (*program_block32_protect) (adapter_t *adapter,
        unsigned nwords, unsigned base, unsigned addr, unsigned *data,
        unsigned addr_odd, unsigned addr_even,
        unsigned cmd_aa, unsigned cmd_55, unsigned cmd_a0);
    void (*program_block64) (adapter_t *adapter,
        unsigned nwords, unsigned base, unsigned addr, unsigned *data,
        unsigned addr_odd, unsigned addr_even,
        unsigned cmd_aa, unsigned cmd_55, unsigned cmd_a0);
#endif
};

adapter_t *adapter_open_usb (int need_reset);
adapter_t *adapter_open_lpt (void);
adapter_t *adapter_open_bitbang (void);
adapter_t *adapter_open_mpsse (void);

void mdelay (unsigned msec);
extern int debug_level;
