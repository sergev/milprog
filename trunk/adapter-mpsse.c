/*
 * Интерфейс через адаптер FT2232 к процессору Элвис Мультикор.
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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <usb.h>

#include "adapter.h"
#include "arm-jtag.h"

typedef struct {
    /* Общая часть */
    adapter_t adapter;

    /* Доступ к устройству через libusb. */
    usb_dev_handle *usbdev;

    /* Буфер для посылаемого пакета MPSSE. */
    unsigned char output [256*16];
    int bytes_to_write;

    /* Буфер для принятых данных. */
    unsigned char input [64];
    int bytes_to_read;
    int bytes_per_word;
    unsigned long long fix_high_bit;
    unsigned long long high_byte_mask;
    unsigned long long high_bit_mask;
    unsigned high_byte_bits;
} mpsse_adapter_t;

/*
 * Можно использовать готовый адаптер Olimex ARM-USB-Tiny с переходником
 * с разъёма ARM 2x10 на разъём MIPS 2x5:
 *
 * Сигнал   Контакт ARM       Контакт MIPS
 * ------------------------------------
 * /TRST        3               3
 *  TDI         5               7
 *  TMS         7               5
 *  TCK         9               1
 *  TDO         13              9
 * /SYSRST      15              6
 *  GND         4,6,8,10,12,    2,8
 *              14,16,18,20
 */

/*
 * Identifiers of USB adapter.
 */
#define OLIMEX_VID              0x15ba
#define OLIMEX_ARM_USB_TINY     0x0004  /* ARM-USB-Tiny */
#define OLIMEX_ARM_USB_TINY_H   0x002a	/* ARM-USB-Tiny-H */

/*
 * USB endpoints.
 */
#define IN_EP                   0x02
#define OUT_EP                  0x81

/* Requests */
#define SIO_RESET               0 /* Reset the port */
#define SIO_MODEM_CTRL          1 /* Set the modem control register */
#define SIO_SET_FLOW_CTRL       2 /* Set flow control register */
#define SIO_SET_BAUD_RATE       3 /* Set baud rate */
#define SIO_SET_DATA            4 /* Set the data characteristics of the port */
#define SIO_POLL_MODEM_STATUS   5
#define SIO_SET_EVENT_CHAR      6
#define SIO_SET_ERROR_CHAR      7
#define SIO_SET_LATENCY_TIMER   9
#define SIO_GET_LATENCY_TIMER   10
#define SIO_SET_BITMODE         11
#define SIO_READ_PINS           12
#define SIO_READ_EEPROM         0x90
#define SIO_WRITE_EEPROM        0x91
#define SIO_ERASE_EEPROM        0x92

/* Биты регистра IRd */
#define IRd_RUN                 0x20    /* 0 - step mode, 1 - run continuosly */
#define IRd_READ                0x40    /* 0 - write, 1 - read registers */
#define IRd_FLUSH_PIPE          0x40    /* for EnGO: instruction pipe changed */
#define IRd_STEP_1CLK           0x80    /* for step mode: run for 1 clock only */

/* Команды MPSSE. */
#define CLKWNEG                 0x01
#define BITMODE                 0x02
#define CLKRNEG                 0x04
#define LSB                     0x08
#define WTDI                    0x10
#define RTDO                    0x20
#define WTMS                    0x40

/*
 * Посылка пакета данных USB-устройству.
 */
static void bulk_write (mpsse_adapter_t *a, unsigned char *output, int nbytes)
{
    int bytes_written;

    if (debug_level > 1) {
        int i;
        fprintf (stderr, "usb bulk write %d bytes:", nbytes);
        for (i=0; i<nbytes; i++)
            fprintf (stderr, "%c%02x", i ? '-' : ' ', output[i]);
        fprintf (stderr, "\n");
    }
    bytes_written = usb_bulk_write (a->usbdev, IN_EP, (char*) output,
        nbytes, 1000);
    if (bytes_written < 0) {
        fprintf (stderr, "usb bulk write failed\n");
        exit (-1);
    }
    if (bytes_written != nbytes)
        fprintf (stderr, "usb bulk written %d bytes of %d",
            bytes_written, nbytes);
}

/*
 * Если в выходном буфере есть накопленные данные -
 * отправка их устройству.
 */
static void mpsse_flush_output (mpsse_adapter_t *a)
{
    int bytes_read, n;
    unsigned char reply [64];

    if (a->bytes_to_write <= 0)
        return;

    bulk_write (a, a->output, a->bytes_to_write);
    a->bytes_to_write = 0;
    if (a->bytes_to_read <= 0)
        return;

    /* Получаем ответ. */
    bytes_read = 0;
    while (bytes_read < a->bytes_to_read) {
        n = usb_bulk_read (a->usbdev, OUT_EP, (char*) reply,
            a->bytes_to_read - bytes_read + 2, 2000);
        if (n < 0) {
            fprintf (stderr, "usb bulk read failed\n");
            exit (-1);
        }
        if (debug_level > 1) {
            if (n != a->bytes_to_read + 2)
                fprintf (stderr, "usb bulk read %d bytes of %d\n",
                    n, a->bytes_to_read - bytes_read + 2);
            else {
                int i;
                fprintf (stderr, "usb bulk read %d bytes:", n);
                for (i=0; i<n; i++)
                    fprintf (stderr, "%c%02x", i ? '-' : ' ', reply[i]);
                fprintf (stderr, "\n");
            }
        }
        if (n > 2) {
            /* Copy data. */
            memcpy (a->input + bytes_read, reply + 2, n - 2);
            bytes_read += n - 2;
        }
    }
    if (debug_level > 1) {
        int i;
        fprintf (stderr, "mpsse_flush_output received %d bytes:", a->bytes_to_read);
        for (i=0; i<a->bytes_to_read; i++)
            fprintf (stderr, "%c%02x", i ? '-' : ' ', a->input[i]);
        fprintf (stderr, "\n");
    }
    a->bytes_to_read = 0;
}

static void mpsse_send (mpsse_adapter_t *a,
    unsigned tms_prolog_nbits, unsigned tms_prolog,
    unsigned tdi_nbits, unsigned long long tdi, int read_flag)
{
    unsigned tms_epilog_nbits = 0, tms_epilog = 0;

    if (tdi_nbits > 0) {
        /* Если есть данные, добавляем:
         * стандартный пролог TMS 1-0-0,
         * стандартный эпилог TMS 1. */
        tms_prolog |= 1 << tms_prolog_nbits;
        tms_prolog_nbits += 3;
        tms_epilog = 1;
        tms_epilog_nbits = 1;
    }
    /* Проверяем, есть ли место в выходном буфере.
     * Максимальный размер одного пакета - 23 байта (6+8+3+3+3). */
    if (a->bytes_to_write > sizeof (a->output) - 23)
        mpsse_flush_output (a);

    /* Формируем пакет команд MPSSE. */
    if (tms_prolog_nbits > 0) {
        /* Пролог TMS, от 1 до 14 бит.
         * 4b - Clock Data to TMS Pin (no Read) */
        a->output [a->bytes_to_write++] = WTMS + BITMODE + CLKWNEG + LSB;
        if (tms_prolog_nbits < 8) {
            a->output [a->bytes_to_write++] = tms_prolog_nbits - 1;
            a->output [a->bytes_to_write++] = tms_prolog;
        } else {
            a->output [a->bytes_to_write++] = 7 - 1;
            a->output [a->bytes_to_write++] = tms_prolog & 0x7f;
            a->output [a->bytes_to_write++] = WTMS + BITMODE + CLKWNEG + LSB;
            a->output [a->bytes_to_write++] = tms_prolog_nbits - 7 - 1;
            a->output [a->bytes_to_write++] = tms_prolog >> 7;
        }
    }
    if (tdi_nbits > 0) {
        /* Данные, от 1 до 64 бит. */
        if (tms_epilog_nbits > 0) {
            /* Последний бит надо сопровождать сигналом TMS=1. */
            tdi_nbits--;
        }
        unsigned nbytes = tdi_nbits / 8;
        unsigned last_byte_bits = tdi_nbits & 7;
        if (read_flag) {
            a->high_byte_bits = last_byte_bits;
            a->fix_high_bit = 0;
            a->high_byte_mask = 0;
            a->bytes_per_word = nbytes;
            if (a->high_byte_bits > 0)
                a->bytes_per_word++;
            a->bytes_to_read += a->bytes_per_word;
        }
        if (nbytes > 0) {
            /* Целые байты.
             * 39 - Clock Data Bytes In and Out LSB First
             * 19 - Clock Data Bytes Out LSB First (no Read) */
            a->output [a->bytes_to_write++] = read_flag ?
                (WTDI + RTDO + CLKWNEG + LSB) :
                (WTDI + CLKWNEG + LSB);
            a->output [a->bytes_to_write++] = nbytes - 1;
            a->output [a->bytes_to_write++] = (nbytes - 1) >> 8;
            while (nbytes-- > 0) {
                a->output [a->bytes_to_write++] = tdi;
                tdi >>= 8;
            }
        }
        if (last_byte_bits) {
            /* Последний нецелый байт.
             * 3b - Clock Data Bits In and Out LSB First
             * 1b - Clock Data Bits Out LSB First (no Read) */
            a->output [a->bytes_to_write++] = read_flag ?
                (WTDI + RTDO + BITMODE + CLKWNEG + LSB) :
                (WTDI + BITMODE + CLKWNEG + LSB);
            a->output [a->bytes_to_write++] = last_byte_bits - 1;
            a->output [a->bytes_to_write++] = tdi;
            tdi >>= last_byte_bits;
            a->high_byte_mask = 0xffULL << (a->bytes_per_word - 1) * 8;
        }
        if (tms_epilog_nbits > 0) {
            /* Последний бит, точнее два.
             * 6b - Clock Data to TMS Pin with Read
             * 4b - Clock Data to TMS Pin (no Read) */
            tdi_nbits++;
            a->output [a->bytes_to_write++] = read_flag ?
                (WTMS + RTDO + BITMODE + CLKWNEG + LSB) :
                (WTMS + BITMODE + CLKWNEG + LSB);
            a->output [a->bytes_to_write++] = 1;
            a->output [a->bytes_to_write++] = tdi << 7 | 1 | tms_epilog << 1;
            tms_epilog_nbits--;
            tms_epilog >>= 1;
            if (read_flag) {
                /* Последний бит придёт в следующем байте.
                 * Вычисляем маску для коррекции. */
                a->fix_high_bit = 0x40ULL << (a->bytes_per_word * 8);
                a->bytes_per_word++;
                a->bytes_to_read++;
            }
        }
        if (read_flag)
            a->high_bit_mask = 1ULL << (tdi_nbits - 1);
    }
    if (tms_epilog_nbits > 0) {
        /* Эпилог TMS, от 1 до 7 бит.
         * 4b - Clock Data to TMS Pin (no Read) */
        a->output [a->bytes_to_write++] = WTMS + BITMODE + CLKWNEG + LSB;
        a->output [a->bytes_to_write++] = tms_epilog_nbits - 1;
        a->output [a->bytes_to_write++] = tms_epilog;
    }
}

static unsigned long long mpsse_fix_data (mpsse_adapter_t *a, unsigned long long word)
{
    unsigned long long fix_high_bit = word & a->fix_high_bit;
    //if (debug) fprintf (stderr, "fix (%08llx) high_bit=%08llx\n", word, a->fix_high_bit);

    if (a->high_byte_bits) {
        /* Корректируем старший байт принятых данных. */
        unsigned long long high_byte = a->high_byte_mask &
            ((word & a->high_byte_mask) >> (8 - a->high_byte_bits));
        word = (word & ~a->high_byte_mask) | high_byte;
        //if (debug) fprintf (stderr, "Corrected byte %08llx -> %08llx\n", a->high_byte_mask, high_byte);
    }
    word &= a->high_bit_mask - 1;
    if (fix_high_bit) {
        /* Корректируем старший бит принятых данных. */
        word |= a->high_bit_mask;
        //if (debug) fprintf (stderr, "Corrected bit %08llx -> %08llx\n", a->high_bit_mask, word >> 9);
    }
    return word;
}

static unsigned long long mpsse_recv (mpsse_adapter_t *a)
{
    unsigned long long word;

    /* Шлём пакет. */
    mpsse_flush_output (a);

    /* Обрабатываем одно слово. */
    memcpy (&word, a->input, sizeof (word));
    return mpsse_fix_data (a, word);
}

static void mpsse_reset (mpsse_adapter_t *a, int trst, int sysrst, int led)
{
    unsigned char output [3];
    unsigned low_output = 0x08; /* TCK idle high */
    unsigned low_direction = 0x1b;
    unsigned high_direction = 0x0f;
    unsigned high_output = 0;

    /* command "set data bits low byte" */
    output [0] = 0x80;
    output [1] = low_output;
    output [2] = low_direction;
    bulk_write (a, output, 3);

    if (! trst)
        high_output |= 1;

    if (sysrst)
        high_output |= 2;

    if (led)
        high_output |= 8;

    /* command "set data bits high byte" */
    output [0] = 0x82;
    output [1] = high_output;
    output [2] = high_direction;

    bulk_write (a, output, 3);
    if (debug_level)
        fprintf (stderr, "mpsse_reset (trst=%d, sysrst=%d) high_output=0x%2.2x, high_direction: 0x%2.2x\n",
            trst, sysrst, high_output, high_direction);
}

static void mpsse_speed (mpsse_adapter_t *a, int divisor)
{
    unsigned char output [3];

    /* command "set TCK divisor" */
    output [0] = 0x86;
    output [1] = divisor;
    output [2] = divisor >> 8;
    bulk_write (a, output, 3);
}

static void mpsse_close (adapter_t *adapter)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    mpsse_flush_output (a);
    mpsse_reset (a, 0, 0, 0);
    usb_release_interface (a->usbdev, 0);
    usb_close (a->usbdev);
    free (a);
}

/*
 * Read the Device Identification code
 */
static unsigned mpsse_get_idcode (adapter_t *adapter)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;
    unsigned idcode;

    /* Reset the JTAG TAP controller: TMS 1-1-1-1-1-0.
     * After reset, the IDCODE register is always selected.
     * Read out 32 bits of data. */
    mpsse_send (a, 6, 31, 32, 0, 1);
    idcode = mpsse_recv (a);
    return idcode;
}

/*
 * Запись регистра DP.
 */
static void mpsse_dp_write (adapter_t *adapter, int reg, unsigned value)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    mpsse_send (a, 1, 1, 4, JTAG_IR_DPACC, 0);
    mpsse_send (a, 0, 0, 32 + 3, (reg >> 1) |
        (unsigned long long) value << 3, 0);
    if (debug_level > 1) {
        fprintf (stderr, "DP write %08x to %s (%02x)\n", value,
            DP_REGNAME(reg), reg);
    }
}

/*
 * Чтение регистра DP.
 */
static unsigned mpsse_dp_read (adapter_t *adapter, int reg)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    /* Читаем содержимое регистра DP. */
    mpsse_send (a, 1, 1, 4, JTAG_IR_DPACC, 0);
    mpsse_send (a, 0, 0, 32 + 3, (reg >> 1) | 1, 0);
    mpsse_send (a, 0, 0, 32 + 3, (DP_RDBUFF >> 1) | 1, 1);
    unsigned long long reply = mpsse_recv (a);

    /* Предыдущая транзакция MEM-AP могла завершиться неуспешно.
     * Анализируем ответ WAIT. */
    adapter->stalled = (((unsigned) reply & 7) != 2) && (((unsigned) reply & 7) != 1);
    //adapter->stalled = ((unsigned) reply & 7) != 2;
    if (adapter->stalled) {
        if (debug_level > 1)
            fprintf (stderr, "DP read <<<WAIT>>> from %s (%02x)\n",
                DP_REGNAME(reg), reg);
        return 0;
    }

    unsigned value = reply >> 3;
    if (debug_level > 1) {
        fprintf (stderr, "DP read %08x from %s (%02x)\n", value,
            DP_REGNAME(reg), reg);
    }
    return value;
}

/*
 * Запись регистра MEM-AP.
 * Старшие биты адреса должны быть предварительно занесены в регистр DP_SELECT.
 */
static void mpsse_mem_ap_write (adapter_t *adapter, int reg, unsigned value)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    /* Пишем в регистр MEM-AP. */
    mpsse_send (a, 1, 1, 4, JTAG_IR_APACC, 0);
    mpsse_send (a, 0, 0, 32 + 3, (reg >> 1 & 6) |
        (unsigned long long) value << 3, 0);
    if (debug_level > 1) {
        fprintf (stderr, "MEM-AP write %08x to %s (%02x)\n", value,
            MEM_AP_REGNAME(reg), reg);
    }
}

/*
 * Чтение регистра MEM-AP.
 * Старшие биты адреса должны быть предварительно занесены в регистр DP_SELECT.
 */
static unsigned mpsse_mem_ap_read (adapter_t *adapter, int reg)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    /* Читаем содержимое регистра MEM-AP. */
    mpsse_send (a, 1, 1, 4, JTAG_IR_APACC, 0);
    mpsse_send (a, 0, 0, 32 + 3, (reg >> 1 & 6) | 1, 0);

    /* Извлекаем прочитанное значение из регистра RDBUFF. */
    mpsse_send (a, 1, 1, 4, JTAG_IR_DPACC, 0);
    mpsse_send (a, 0, 0, 32 + 3, (DP_RDBUFF >> 1) | 1, 1);
    unsigned long long reply = mpsse_recv (a);

    /* Предыдущая транзакция MEM-AP могла завершиться неуспешно.
     * Анализируем ответ WAIT. */
    adapter->stalled = ((unsigned) reply & 7) != 2;
    if (adapter->stalled) {
        if (debug_level > 1)
            fprintf (stderr, "MEM-AP read <<<WAIT>>> from %s (%02x)\n",
                MEM_AP_REGNAME(reg), reg);
        return 0;
    }
    unsigned value = reply >> 3;
    if (debug_level > 1) {
        fprintf (stderr, "MEM-AP read %08x from %s (%02x)\n", value,
            MEM_AP_REGNAME(reg), reg);
    }
    return value;
}

/*
 * Чтение блока памяти.
 * Предварительно в регистр DP_SELECT должен быть занесён 0.
 * Количество слов не больше 10, иначе переполняется буфер USB.
 */
static void mpsse_read_data (adapter_t *adapter,
    unsigned addr, unsigned nwords, unsigned *data)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    /* Пишем адрес в регистр TAR. */
    mpsse_mem_ap_write (adapter, MEM_AP_TAR, addr);

    /* Запрашиваем данные через регистр DRW.
     * Первое чтение не выдаёт значения. */
    mpsse_send (a, 1, 1, 4, JTAG_IR_APACC, 0);
    mpsse_send (a, 0, 0, 32 + 3, (MEM_AP_DRW >> 1 & 6) | 1, 0);
    unsigned i;
    for (i=0; i<nwords; i++) {
        mpsse_send (a, 1, 1, 4, JTAG_IR_APACC, 0);
        mpsse_send (a, 0, 0, 32 + 3, (MEM_AP_DRW >> 1 & 6) | 1, 1);
    }
    /* Шлём пакет. */
    mpsse_flush_output (a);

    /* Извлекаем и обрабатываем данные. */
    for (i=0; i<nwords; i++) {
        unsigned long long reply;
        memcpy (&reply, a->input + i*a->bytes_per_word, sizeof (reply));
        reply = mpsse_fix_data (a, reply);
        adapter->stalled = ((unsigned) reply & 7) != 2;
        data[i] = reply >> 3;
    }
}

/*
 * Аппаратный сброс процессора.
 */
static void mpsse_reset_cpu (adapter_t *adapter)
{
    mpsse_adapter_t *a = (mpsse_adapter_t*) adapter;

    /* Забываем невыполненную транзакцию. */
    a->bytes_to_write = 0;
    a->bytes_to_read = 0;

    /* Активируем /SYSRST на несколько микросекунд. */
    mpsse_reset (a, 0, 1, 1);
    mpsse_reset (a, 0, 0, 1);
}

/*
 * Инициализация адаптера F2232.
 * Возвращаем указатель на структуру данных, выделяемую динамически.
 * Если адаптер не обнаружен, возвращаем 0.
 */
adapter_t *adapter_open_mpsse (void)
{
    mpsse_adapter_t *a;
    struct usb_bus *bus;
    struct usb_device *dev;

    usb_init();
    usb_find_busses();
    usb_find_devices();
    for (bus = usb_get_busses(); bus; bus = bus->next) {
        for (dev = bus->devices; dev; dev = dev->next) {
            if (dev->descriptor.idVendor == OLIMEX_VID &&
                (dev->descriptor.idProduct == OLIMEX_ARM_USB_TINY ||
                 dev->descriptor.idProduct == OLIMEX_ARM_USB_TINY_H))
                goto found;
        }
    }
    /*fprintf (stderr, "USB adapter not found: vid=%04x, pid=%04x\n",
        OLIMEX_VID, OLIMEX_PID);*/
    return 0;
found:
    /*fprintf (stderr, "found USB adapter: vid %04x, pid %04x, type %03x\n",
        dev->descriptor.idVendor, dev->descriptor.idProduct,
        dev->descriptor.bcdDevice);*/
    a = calloc (1, sizeof (*a));
    if (! a) {
        fprintf (stderr, "Out of memory\n");
        return 0;
    }
    a->usbdev = usb_open (dev);
    if (! a->usbdev) {
        fprintf (stderr, "MPSSE adapter: usb_open() failed\n");
        free (a);
        return 0;
    }
    usb_claim_interface (a->usbdev, 0);

    /* Reset the ftdi device. */
    if (usb_control_msg (a->usbdev,
        USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
        SIO_RESET, 0, 1, 0, 0, 1000) != 0) {
        if (errno == EPERM)
            fprintf (stderr, "MPSSE adapter: superuser privileges needed.\n");
        else
            fprintf (stderr, "MPSSE adapter: FTDI reset failed\n");
failed: usb_release_interface (a->usbdev, 0);
        usb_close (a->usbdev);
        free (a);
        return 0;
    }

    /* MPSSE mode. */
    if (usb_control_msg (a->usbdev,
        USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
        SIO_SET_BITMODE, 0x20b, 1, 0, 0, 1000) != 0) {
        fprintf (stderr, "Can't set sync mpsse mode\n");
        goto failed;
    }

    /* Ровно 500 нсек между выдачами. */
    //  Was: divisor = 3, latency_timer = 1
    unsigned divisor = 1;
    unsigned char latency_timer = 1;

    if (usb_control_msg (a->usbdev,
        USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
        SIO_SET_LATENCY_TIMER, latency_timer, 1, 0, 0, 1000) != 0) {
        fprintf (stderr, "unable to set latency timer\n");
        goto failed;
    }
    if (usb_control_msg (a->usbdev,
        USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
        SIO_GET_LATENCY_TIMER, 0, 1, (char*) &latency_timer, 1, 1000) != 1) {
        fprintf (stderr, "unable to get latency timer\n");
        goto failed;
    }
    if (debug_level) {
    	fprintf (stderr, "MPSSE: divisor: %u\n", divisor);
    	fprintf (stderr, "MPSSE: latency timer: %u usec\n", latency_timer);
    }
    mpsse_reset (a, 0, 0, 1);

    if (debug_level) {
     int baud = 6000000 / (divisor + 1);
        fprintf (stderr, "MPSSE: speed %d samples/sec\n", baud);
    }
    mpsse_speed (a, divisor);

    /* Disable TDI to TDO loopback. */
    unsigned char enable_loopback[] = "\x85";
    bulk_write (a, enable_loopback, 1);

    mpsse_reset (a, 1, 1, 1);
    mpsse_reset (a, 0, 0, 1);

    /* Reset the JTAG TAP controller. */
    mpsse_send (a, 6, 31, 0, 0, 0);         /* TMS 1-1-1-1-1-0 */

    /* Обязательные функции. */
    a->adapter.close = mpsse_close;
    a->adapter.get_idcode = mpsse_get_idcode;
    a->adapter.reset_cpu = mpsse_reset_cpu;
    a->adapter.dp_read = mpsse_dp_read;
    a->adapter.dp_write = mpsse_dp_write;
    a->adapter.mem_ap_read = mpsse_mem_ap_read;
    a->adapter.mem_ap_write = mpsse_mem_ap_write;
    a->adapter.read_data = mpsse_read_data;
    return &a->adapter;
}
