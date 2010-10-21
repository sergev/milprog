/*
 * Описание регистров отладочного блока ADI v5.
 * Автор: С.Вакуленко.
 * Источник информации: ARM Debug Interface v5 Architecture Specification.
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

/*
 * JTAG instruction register.
 */
#define JTAG_IR_ABORT		0x8
#define JTAG_IR_DPACC		0xA     /* Access to DP registers */
#define JTAG_IR_APACC		0xB     /* Access to MEM-AP registers */
#define JTAG_IR_IDCODE		0xE     /* Select the ID register */
#define JTAG_IR_BYPASS		0xF

/*
 * JTAG DP registers.
 */
#define DP_ABORT		0x0	/* AP abort (write-only) */
#define DP_CTRL_STAT		0x4	/* Control/status (r/w) */
#define DP_SELECT		0x8	/* AP select (r/w) */
#define DP_RDBUFF		0xC	/* Read buffer (read-only) */

#define DP_REGNAME(reg) (reg) == DP_ABORT ? "ABORT" : \
                        (reg) == DP_CTRL_STAT ? "CTRL/STAT" : \
                        (reg) == DP_SELECT ? "SELECT" : \
                        (reg) == DP_RDBUFF ? "RDBUFF" : "???"
/*
 * Fields of the DP_CTRL_STAT register.
 */
#define CORUNDETECT             (1<<0)  /* enable overrun detection */
#define SSTICKYORUN             (1<<1)  /* overrun detection */
    /* 3:2 - transaction mode (e.g. pushed compare) */
#define SSTICKYCMP              (1<<4)  /* match in a pushed compare */
#define SSTICKYERR              (1<<5)  /* error in AP transaction */
    /* 11:8 - mask lanes for pushed compare or verify ops */
    /* 21:12 - transaction counter */
#define CDBGRSTREQ              (1<<26) /* Debug reset request */
#define CDBGRSTACK              (1<<27) /* Debug reset acknowledge */
#define CDBGPWRUPREQ            (1<<28) /* Debug power-up request */
#define CDBGPWRUPACK            (1<<29) /* Debug power-up acknowledge */
#define CSYSPWRUPREQ            (1<<30) /* System power-up request */
#define CSYSPWRUPACK            (1<<31) /* System power-up acknowledge */

/*
 * JTAG MEM-AP registers.
 */
#define MEM_AP_CSW              0x00    /* Control/status word */
#define MEM_AP_TAR              0x04    /* Transfer address */
#define MEM_AP_DRW              0x0C    /* Data read/write */
#define MEM_AP_BD0              0x10    /* Banked data +0 */
#define MEM_AP_BD1              0x14    /* Banked data +4 */
#define MEM_AP_BD2              0x18    /* Banked data +8 */
#define MEM_AP_BD3              0x1C    /* Banked data +12 */
#define MEM_AP_CFG              0xF4    /* Configuration (read-only) */
#define MEM_AP_BASE             0xF8    /* Debug base address (read-only) */
#define MEM_AP_IDR              0xFC    /* Identification (read-only) */

#define MEM_AP_REGNAME(reg) (reg) == MEM_AP_CSW  ? "CSW" : \
                            (reg) == MEM_AP_TAR  ? "TAR" : \
                            (reg) == MEM_AP_DRW  ? "DRW" : \
                            (reg) == MEM_AP_BD0  ? "BD0" : \
                            (reg) == MEM_AP_BD1  ? "BD1" : \
                            (reg) == MEM_AP_BD2  ? "BD2" : \
                            (reg) == MEM_AP_BD3  ? "BD3" : \
                            (reg) == MEM_AP_CFG  ? "CFG" : \
                            (reg) == MEM_AP_BASE ? "BASE" : \
                            (reg) == MEM_AP_IDR  ? "IDR" : "???"
/*
 * Fields of the MEM_AP_CSW register for Cortex-M3.
 */
#define CSW_8BIT		0       /* Size of the access to perform */
#define CSW_16BIT		1
#define CSW_32BIT		2
#define CSW_ADDRINC_MASK	(3<<4)  /* Address auto-increment and packing mode */
#define CSW_ADDRINC_OFF		0
#define CSW_ADDRINC_SINGLE	(1<<4)
#define CSW_ADDRINC_PACKED	(2<<4)
#define CSW_DEVICE_EN		(1<<6)  /* MEM transfers permitted */
#define CSW_TRIN_PROG		(1<<7)  /* Transfer in progress */
    /* 30:24 - implementation-defined! */
#define CSW_HPROT		(1<<25) /* User/privilege control */
#define CSW_MASTER_DEBUG	(1<<29) /* Allow debugger to halt the core */

/*
 * Fields of the MEM_AP_CFG register.
 */
#define CFG_BIGENDIAN		1       /* Big-endian memory access. */

/*
 * Cortex-M3 registers.
 */
#define SYSTICK_CTRL            0xE000E010
#define ICER0                   0xE000E180      /* Запрет прерываний */
#define ICPR0                   0xE000E280      /* Сброс прерываний */
#define CPUID                   0xE000ED00
#define AIRCR                   0xE000ED0C

/*
 * Регистр SCB AIRCR: управление прерываниями и программный сброс.
 */
#define ARM_AIRCR_VECTKEY	(0x05FA << 16)	/* ключ доступа к регистру */
#define ARM_AIRCR_ENDIANESS	(1 << 15)	/* старший байт идет первым */
#define ARM_AIRCR_PRIGROUP(n)	((n) << 8)	/* группировка приоритетов исключений */
#define ARM_AIRCR_SYSRESETREQ	(1 << 2)	/* запрос сброса системы */

/* Debug Control Block */
#define DCB_DHCSR               0xE000EDF0
#define DCB_DCRSR               0xE000EDF4
#define DCB_DCRDR               0xE000EDF8
#define DCB_DEMCR               0xE000EDFC

#define DCRSR_WnR               (1 << 16)

/* DCB_DHCSR bit and field definitions */
#define DBGKEY                  (0xA05F << 16)
#define C_DEBUGEN               (1 << 0)
#define C_HALT                  (1 << 1)
#define C_STEP                  (1 << 2)
#define C_MASKINTS              (1 << 3)
#define C_SNAPSTALL             (1 << 5)
#define S_REGRDY                (1 << 16)
#define S_HALT                  (1 << 17)
#define S_SLEEP                 (1 << 18)
#define S_LOCKUP                (1 << 19)
#define S_RETIRE_ST             (1 << 24)
#define S_RESET_ST              (1 << 25)

/*
 * Milandr 1986BE9x register definitions.
 */
#define PER_CLOCK               0x4002001C      /* Разрешение тактовой частоты */
#define UART1_CR                0x40030030
#define UART2_CR                0x40038030

#define EEPROM_CMD		0x40018000	/* Управление Flash-памятью */
#define EEPROM_ADR		0x40018004	/* Адрес (словный) */
#define EEPROM_DI		0x40018008	/* Данные для записи */
#define EEPROM_DO		0x4001800C	/* Считанные данные */
#define EEPROM_KEY		0x40018010	/* Ключ */

/*
 * Регистр EEPROM_CMD
 */
#define EEPROM_CMD_CON          0x00000001
				/*
				 * Переключение контроллера памяти EEPROM на
				 * регистровое управление. Не может производиться
				 * при исполнении программы из области EERPOM.
				 * 0 – управление EERPOM от ядра, рабочий режим
				 * 1 – управление от регистров, режим программирования
				 */

#define EEPROM_CMD_WR           0x00000002
				/*
				 * Запись в память EERPOM (в режиме программирования)
				 * 0 – нет записи
				 * 1 – есть запись
				 */

#define EEPROM_CMD_RD           0x00000004
				/*
				 * Чтение из память EERPOM (в режиме программирования)
				 * 0 – нет чтения
				 * 1 – есть чтение
				 */

#define EEPROM_CMD_DELAY_MASK	0x00000038
				/*
				 * Задержка памяти программ при чтении
				 */
#define EEPROM_CMD_DELAY_0	0x00000000	/* 0 тактов - до 25 МГц */
#define EEPROM_CMD_DELAY_1	0x00000008      /* 1 такт - до 50 МГц */
#define EEPROM_CMD_DELAY_2	0x00000010      /* 2 такта - до 75 МГц */
#define EEPROM_CMD_DELAY_3	0x00000018      /* 3 такта - до 100 МГц */
#define EEPROM_CMD_DELAY_4	0x00000020      /* 4 такта - до 125 МГц */
#define EEPROM_CMD_DELAY_5	0x00000028      /* 5 тактов - до 150 МГц */
#define EEPROM_CMD_DELAY_6	0x00000030      /* 6 тактов - до 175 МГц */
#define EEPROM_CMD_DELAY_7	0x00000038      /* 7 тактов - до 200 МГц */

#define EEPROM_CMD_XE           0x00000040
				/*
				 * Выдача адреса ADR[16:9]
				 * 0 – не разрешено
				 * 1 - разрешено
				 */

#define EEPROM_CMD_YE           0x00000080
				/*
				 * Выдача адреса ADR[8:2]
				 * 0 – не разрешено
				 * 1 – разрешено
				 */

#define EEPROM_CMD_SE           0x00000100
				/*
				 * Усилитель считывания
				 * 0 – не включен
				 * 1 – включен
				 */

#define EEPROM_CMD_IFREN 	0x00000200
				/*
				 * Работа с блоком информации
				 * 0 – основная память
				 * 1 – информационный блок
				 */

#define EEPROM_CMD_ERASE 	0x00000400
				/*
				 * Стереть строку с адресом ADR[16:9].
				 * ADR[8:0] значения не имеет.
				 * 0 – нет стирания
				 * 1 – стирание
				 */

#define EEPROM_CMD_MAS1 	0x00000800
				/*
				 * Стереть весь блок, при ERASE=1
				 * 0 – нет стирания
				 * 1 – стирание
				 */

#define EEPROM_CMD_PROG 	0x00001000
				/*
				 * Записать данные по ADR[16:2] из регистра EERPOM_DI
				 * 0 – нет записи
				 * 1 – есть запись
				 */

#define EEPROM_CMD_NVSTR	0x00002000
				/*
				 * Операции записи или стирания
				 * 0 – при чтении
				 * 1 - при записи или стирании
				 */
