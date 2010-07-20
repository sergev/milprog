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

/*
 * Fields of the MEM_AP_CSW register.
 */
#define CSW_8BIT		0       /* Size of the access to perform */
#define CSW_16BIT		1
#define CSW_32BIT		2
#define CSW_ADDRINC_MASK	(3<<4)  /* Address auto-increment and packing mode */
#define CSW_ADDRINC_OFF		0
#define CSW_ADDRINC_SINGLE	(1<<4)
#define CSW_ADDRINC_PACKED	(2<<4)
#define CSW_DEVICE_EN		(1<<6)  /* Device enabled */
#define CSW_TRIN_PROG		(1<<7)  /* Transfer in progress */
#define CSW_SPIDEN		(1<<23) /* Secure privileged debug enabled */
    /* 30:24 - implementation-defined! */
#define CSW_HPROT		(1<<25) /* ? */
#define CSW_MASTER_DEBUG	(1<<29) /* ? */
#define CSW_DBGSWENABLE		(1<<31) /* Debug software access enable */

/*
 * Fields of the MEM_AP_CFG register.
 */
#define CFG_BIGENDIAN		1       /* Big-endian memory access. */
