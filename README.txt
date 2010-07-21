Утилита MILPROG предназначена для записи программного обеспечения
в Flash-память микроконтроллеров Миландр с архитектурой ARM
(1986ВМ91Т, 1986ВМ92УТ, 1986ВМ93У). В качестве программатора
используется адаптер Olimex ARM-USB-Tiny.


=== Вызов ===

При вызове без параметров утилита MILPROG определяет тип процессора
и flash-памяти, установленных на плате. Например:

    % milprog
    Programmer for Milandr ARM microcontrollers, Version 1.0
    Copyright (C) 2010 Serge Vakulenko.
    Processor: Milandr 1986BM91T (id 412FC230)
    Flash memory: 128 kbytes

Запись в flash-память:

    milprog [-v] file.srec
    milprog [-v] file.bin [address]

Запись в статическую память:

    milprog -w [-v] file.sreс
    milprog -w [-v] file.bin [address]

Чтение памяти в файл:

    milprog -r file.bin address length

Параметры:

    file.srec   - файл с прошивкой в формате SREC
    file.bin    - бинарный файл с прошивкой
    address     - адрес flash-памяти, по умолчанию 0x08000000
    -v          - без записи, только проверка памяти на совпадение
    -w          - запись в статическую память
    -r          - режим чтения

При завершении работы утилита производит аппаратный сброс процессора
(сигнал /SYSRST).

Входной файл должен иметь простой бинарный формат, или SREC. Формат SREC
предпочтительнее, так как в нём имеется информация об адресах программы.
Преобразовать формат ELF или COFF или A.OUT в SREC можно командой objcopy,
например:

    objcopy -O srec firmware.elf firmware.srec


=== Исходные тексты ===

Исходные тексты распространяются на условиях лицензии GPL. Их можно
скачать через SVN командой:

    svn checkout http://milprog.googlecode.com/svn/trunk/ milprog

___
С уважением,
Сергей Вакуленко
