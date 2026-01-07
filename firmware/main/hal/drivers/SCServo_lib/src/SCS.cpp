/*
 * SCS.cpp
 * FIT serial servo communication layer protocol program
 */

#include <stddef.h>
#include "SCS.h"

SCS::SCS()
{
    Level = 1;  // All commands except broadcast commands return responses
    Error = 0;
}

SCS::SCS(u8 End)
{
    Level     = 1;
    this->End = End;
    Error     = 0;
}

SCS::SCS(u8 End, u8 Level)
{
    this->Level = Level;
    this->End   = End;
    Error       = 0;
}

// Split a 16-bit number into two 8-bit numbers
// DataL is the low bit, DataH is the high bit
void SCS::Host2SCS(u8 *DataL, u8 *DataH, u16 Data)
{
    if (End) {
        *DataL = (Data >> 8);
        *DataH = (Data & 0xff);
    } else {
        *DataH = (Data >> 8);
        *DataL = (Data & 0xff);
    }
}

// 8-bit numbers are combined into a 16-bit number
// DataL is the low bit, DataH is the high bit
u16 SCS::SCS2Host(u8 DataL, u8 DataH)
{
    u16 Data;
    if (End) {
        Data = DataL;
        Data <<= 8;
        Data |= DataH;
    } else {
        Data = DataH;
        Data <<= 8;
        Data |= DataL;
    }
    return Data;
}

void SCS::writeBuf(u8 ID, u8 MemAddr, u8 *nDat, u8 nLen, u8 Fun)
{
    u8 msgLen = 2;
    u8 bBuf[6];
    u8 CheckSum = 0;
    bBuf[0]     = 0xff;
    bBuf[1]     = 0xff;
    bBuf[2]     = ID;
    bBuf[4]     = Fun;
    if (nDat) {
        msgLen += nLen + 1;
        bBuf[3] = msgLen;
        bBuf[5] = MemAddr;
        writeSCS(bBuf, 6);

    } else {
        bBuf[3] = msgLen;
        writeSCS(bBuf, 5);
    }
    CheckSum = ID + msgLen + Fun + MemAddr;
    u8 i     = 0;
    if (nDat) {
        for (i = 0; i < nLen; i++) {
            CheckSum += nDat[i];
        }
        writeSCS(nDat, nLen);
    }
    writeSCS(~CheckSum);
}

// Normal write command
// Servo ID, MemAddr memory table address, write data, write length
int SCS::genWrite(u8 ID, u8 MemAddr, u8 *nDat, u8 nLen)
{
    rFlushSCS();
    writeBuf(ID, MemAddr, nDat, nLen, INST_WRITE);
    wFlushSCS();
    return Ack(ID);
}

// Asynchronous write command
// Servo ID, MemAddr memory table address, write data, write length
int SCS::regWrite(u8 ID, u8 MemAddr, u8 *nDat, u8 nLen)
{
    rFlushSCS();
    writeBuf(ID, MemAddr, nDat, nLen, INST_REG_WRITE);
    wFlushSCS();
    return Ack(ID);
}

// Asynchronous write execution command
// Servo ID
int SCS::RegWriteAction(u8 ID)
{
    rFlushSCS();
    writeBuf(ID, 0, NULL, 0, INST_REG_ACTION);
    wFlushSCS();
    return Ack(ID);
}

// Synchronous write command
// Servo ID[] array, IDN array length, MemAddr memory table address, write data, write length
void SCS::syncWrite(u8 ID[], u8 IDN, u8 MemAddr, u8 *nDat, u8 nLen)
{
    rFlushSCS();
    u8 mesLen = ((nLen + 1) * IDN + 4);
    u8 Sum    = 0;
    u8 bBuf[7];
    bBuf[0] = 0xff;
    bBuf[1] = 0xff;
    bBuf[2] = 0xfe;
    bBuf[3] = mesLen;
    bBuf[4] = INST_SYNC_WRITE;
    bBuf[5] = MemAddr;
    bBuf[6] = nLen;
    writeSCS(bBuf, 7);

    Sum = 0xfe + mesLen + INST_SYNC_WRITE + MemAddr + nLen;
    u8 i, j;
    for (i = 0; i < IDN; i++) {
        writeSCS(ID[i]);
        writeSCS(nDat + i * nLen, nLen);
        Sum += ID[i];
        for (j = 0; j < nLen; j++) {
            Sum += nDat[i * nLen + j];
        }
    }
    writeSCS(~Sum);
    wFlushSCS();
}

int SCS::writeByte(u8 ID, u8 MemAddr, u8 bDat)
{
    rFlushSCS();
    writeBuf(ID, MemAddr, &bDat, 1, INST_WRITE);
    wFlushSCS();
    return Ack(ID);
}

int SCS::writeWord(u8 ID, u8 MemAddr, u16 wDat)
{
    u8 bBuf[2];
    Host2SCS(bBuf + 0, bBuf + 1, wDat);
    rFlushSCS();
    writeBuf(ID, MemAddr, bBuf, 2, INST_WRITE);
    wFlushSCS();
    return Ack(ID);
}

// Read command
// Servo ID, MemAddr memory table address, return data nData, data length nLen
int SCS::Read(u8 ID, u8 MemAddr, u8 *nData, u8 nLen)
{
    rFlushSCS();
    writeBuf(ID, MemAddr, &nLen, 1, INST_READ);
    wFlushSCS();
    if (!checkHead()) {
        return 0;
    }
    u8 bBuf[4];
    Error = 0;
    if (readSCS(bBuf, 3) != 3) {
        return 0;
    }
    int Size = readSCS(nData, nLen);
    if (Size != nLen) {
        return 0;
    }
    if (readSCS(bBuf + 3, 1) != 1) {
        return 0;
    }
    u8 calSum = bBuf[0] + bBuf[1] + bBuf[2];
    u8 i;
    for (i = 0; i < Size; i++) {
        calSum += nData[i];
    }
    calSum = ~calSum;
    if (calSum != bBuf[3]) {
        return 0;
    }
    Error = bBuf[2];
    return Size;
}

// Read 1 byte, return -1 if timeout
int SCS::readByte(u8 ID, u8 MemAddr)
{
    u8 bDat;
    int Size = Read(ID, MemAddr, &bDat, 1);
    if (Size != 1) {
        return -1;
    } else {
        return bDat;
    }
}

// Read 2 bytes, return -1 if timeout
int SCS::readWord(u8 ID, u8 MemAddr)
{
    u8 nDat[2];
    int Size;
    u16 wDat;
    Size = Read(ID, MemAddr, nDat, 2);
    if (Size != 2) return -1;
    wDat = SCS2Host(nDat[0], nDat[1]);
    return wDat;
}

// Ping command, return the servo ID, timeout returns -1
int SCS::Ping(u8 ID)
{
    rFlushSCS();
    writeBuf(ID, 0, NULL, 0, INST_PING);
    wFlushSCS();
    Error = 0;
    if (!checkHead()) {
        return -1;
    }
    u8 bBuf[4];
    if (readSCS(bBuf, 4) != 4) {
        return -1;
    }
    if (bBuf[0] != ID && ID != 0xfe) {
        return -1;
    }
    if (bBuf[1] != 2) {
        return -1;
    }
    u8 calSum = ~(bBuf[0] + bBuf[1] + bBuf[2]);
    if (calSum != bBuf[3]) {
        return -1;
    }
    Error = bBuf[2];
    return bBuf[0];
}

int SCS::checkHead()
{
    u8 bDat;
    u8 bBuf[2] = {0, 0};
    u8 Cnt     = 0;
    while (1) {
        if (!readSCS(&bDat, 1)) {
            return 0;
        }
        bBuf[1] = bBuf[0];
        bBuf[0] = bDat;
        if (bBuf[0] == 0xff && bBuf[1] == 0xff) {
            break;
        }
        Cnt++;
        if (Cnt > 10) {
            return 0;
        }
    }
    return 1;
}

int SCS::Ack(u8 ID)
{
    Error = 0;
    if (ID != 0xfe && Level) {
        if (!checkHead()) {
            return 0;
        }
        u8 bBuf[4];
        if (readSCS(bBuf, 4) != 4) {
            return 0;
        }
        if (bBuf[0] != ID) {
            return 0;
        }
        if (bBuf[1] != 2) {
            return 0;
        }
        u8 calSum = ~(bBuf[0] + bBuf[1] + bBuf[2]);
        if (calSum != bBuf[3]) {
            return 0;
        }
        Error = bBuf[2];
    }
    return 1;
}

int SCS::syncReadPacketTx(u8 ID[], u8 IDN, u8 MemAddr, u8 nLen)
{
    syncReadRxPacketLen = nLen;
    u8 checkSum         = (4 + 0xfe) + IDN + MemAddr + nLen + INST_SYNC_READ;
    u8 i;
    writeSCS(0xff);
    writeSCS(0xff);
    writeSCS(0xfe);
    writeSCS(IDN + 4);
    writeSCS(INST_SYNC_READ);
    writeSCS(MemAddr);
    writeSCS(nLen);
    for (i = 0; i < IDN; i++) {
        writeSCS(ID[i]);
        checkSum += ID[i];
    }
    checkSum = ~checkSum;
    writeSCS(checkSum);
    return nLen;
}

int SCS::syncReadPacketRx(u8 ID, u8 *nDat)
{
    syncReadRxPacket      = nDat;
    syncReadRxPacketIndex = 0;
    u8 bBuf[4];
    if (!checkHead()) {
        return 0;
    }
    if (readSCS(bBuf, 3) != 3) {
        return 0;
    }
    if (bBuf[0] != ID) {
        return 0;
    }
    if (bBuf[1] != (syncReadRxPacketLen + 2)) {
        return 0;
    }
    Error = bBuf[2];
    if (readSCS(nDat, syncReadRxPacketLen) != syncReadRxPacketLen) {
        return 0;
    }
    return syncReadRxPacketLen;
}

int SCS::syncReadRxPacketToByte()
{
    if (syncReadRxPacketIndex >= syncReadRxPacketLen) {
        return -1;
    }
    return syncReadRxPacket[syncReadRxPacketIndex++];
}

int SCS::syncReadRxPacketToWrod(u8 negBit)
{
    if ((syncReadRxPacketIndex + 1) >= syncReadRxPacketLen) {
        return -1;
    }
    int Word = SCS2Host(syncReadRxPacket[syncReadRxPacketIndex], syncReadRxPacket[syncReadRxPacketIndex + 1]);
    syncReadRxPacketIndex += 2;
    if (negBit) {
        if (Word & (1 << negBit)) {
            Word = -(Word & ~(1 << negBit));
        }
    }
    return Word;
}
