/*
 * INST.h
 * FIT serial servo protocol command definition
 */

#ifndef _INST_H
#define _INST_H

#include <stdint.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

#define INST_PING       0x01
#define INST_READ       0x02
#define INST_WRITE      0x03
#define INST_REG_WRITE  0x04
#define INST_REG_ACTION 0x05
#define INST_SYNC_READ  0x82
#define INST_SYNC_WRITE 0x83

// Baud rate definition
#define _1M     0
#define _0_5M   1
#define _250K   2
#define _128K   3
#define _115200 4
#define _76800  5
#define _57600  6
#define _38400  7
#define _19200  8
#define _14400  9
#define _9600   10
#define _4800   11

#endif