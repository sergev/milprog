/*
 * Программный интерфейс управления целевым процессором
 * через адаптер JTAG.
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

#define NFLASH      16  /* Max flash regions. */

typedef struct _target_t target_t;

target_t *target_open (int need_reset);
void target_close (target_t *mc);

unsigned target_idcode (target_t *mc);
const char *target_cpu_name (target_t *mc);

unsigned target_flash_width (target_t *mc);
unsigned target_flash_bytes (target_t *mc);
void target_flash_configure (target_t *mc, unsigned first, unsigned last);
unsigned target_flash_next (target_t *mc, unsigned prev, unsigned *last);

int target_erase (target_t *mc, unsigned addr);
void target_program_block (target_t *mc, unsigned addr,
	unsigned nwords, unsigned *data);

unsigned target_read_word (target_t *mc, unsigned addr);
void target_read_block (target_t *mc, unsigned addr,
	unsigned nwords, unsigned *data);

void target_write_word (target_t *mc, unsigned addr, unsigned word);
void target_write_block (target_t *mc, unsigned addr,
	unsigned nwords, unsigned *data);
