/*
 * SCSerial.h
 * FIT serial servo hardware interface layer program
 */

#ifndef _SCSERIAL_H
#define _SCSERIAL_H

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "SCS.h"

class SCSerial : public SCS {
public:
    SCSerial();
    SCSerial(u8 End);
    SCSerial(u8 End, u8 Level);

    // Initialize UART
    bool begin(uart_port_t uart_num, int baud_rate, int tx_pin, int rx_pin, int buf_size = 1024);
    void end();

protected:
    virtual int writeSCS(unsigned char *nDat, int nLen);  // Output nLen bytes
    virtual int readSCS(unsigned char *nDat, int nLen);   // Input nLen bytes
    virtual int writeSCS(unsigned char bDat);             // Output 1 byte
    virtual void rFlushSCS();
    virtual void wFlushSCS();

public:
    unsigned long int IOTimeOut;  // Input and output timeout (in milliseconds)
    uart_port_t uart_num;         // UART port number
    int Err;

public:
    virtual int getErr()
    {
        return Err;
    }
};

#endif