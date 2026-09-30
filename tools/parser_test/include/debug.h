/* Host stand-in for QMK's debug.h: the parser's debug prints go nowhere. */
#pragma once
#define dprintf(...) ((void)0)
#define dprintln(...) ((void)0)
