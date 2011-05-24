/*
 * Русификация посредством gettext.
 *
 * Copyright (C) 2010 Serge Vakulenko
 */
#if 1
    /* Никакой локализации, всё по-английски. */
    #define _(str)                      (str)
    #define N_(str)                     str
    #define textdomain(name)            /* empty */
    #define bindtextdomain(name,dir)    /* empty */
#else
    /* Локализация посредством gettext(). */
    #include <libintl.h>
    #define _(str)                      gettext (str)
    #define gettext_noop(str)           str
    #define N_(str)                     gettext_noop (str)
#endif
