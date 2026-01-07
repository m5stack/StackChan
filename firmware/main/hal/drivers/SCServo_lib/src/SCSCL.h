/*
 * SCSCL.h
 * FIT SCSCL series serial servo application layer program
 */

#ifndef _SCSCL_H
#define _SCSCL_H

// Memory table definition
//-------EPROM (read-only)--------
#define SCSCL_VERSION_L 3
#define SCSCL_VERSION_H 4

//-------EPROM (read and write)--------
#define SCSCL_ID                5
#define SCSCL_BAUD_RATE         6
#define SCSCL_MIN_ANGLE_LIMIT_L 9
#define SCSCL_MIN_ANGLE_LIMIT_H 10
#define SCSCL_MAX_ANGLE_LIMIT_L 11
#define SCSCL_MAX_ANGLE_LIMIT_H 12
#define SCSCL_CW_DEAD           26
#define SCSCL_CCW_DEAD          27

//-------SRAM (read and write)--------
#define SCSCL_TORQUE_ENABLE   40
#define SCSCL_GOAL_POSITION_L 42
#define SCSCL_GOAL_POSITION_H 43
#define SCSCL_GOAL_TIME_L     44
#define SCSCL_GOAL_TIME_H     45
#define SCSCL_GOAL_SPEED_L    46
#define SCSCL_GOAL_SPEED_H    47
#define SCSCL_LOCK            48

//-------SRAM (read only)--------
#define SCSCL_PRESENT_POSITION_L  56
#define SCSCL_PRESENT_POSITION_H  57
#define SCSCL_PRESENT_SPEED_L     58
#define SCSCL_PRESENT_SPEED_H     59
#define SCSCL_PRESENT_LOAD_L      60
#define SCSCL_PRESENT_LOAD_H      61
#define SCSCL_PRESENT_VOLTAGE     62
#define SCSCL_PRESENT_TEMPERATURE 63
#define SCSCL_MOVING              66
#define SCSCL_PRESENT_CURRENT_L   69
#define SCSCL_PRESENT_CURRENT_H   70

#include "SCSerial.h"

class SCSCL : public SCSerial {
public:
    SCSCL();
    SCSCL(u8 End);
    SCSCL(u8 End, u8 Level);
    virtual int WritePos(u8 ID, u16 Position, u16 Time, u16 Speed);  // Normal write of single servo position command
    virtual int WritePosEx(u8 ID, s16 Position, u16 Speed, u8 ACC);  // Single servo position command
    virtual int RegWritePos(
        u8 ID, u16 Position, u16 Time,
        u16 Speed = 0);  // Asynchronously write a single servo position command (RegWriteAction takes effect)
    virtual void SyncWritePos(u8 ID[], u8 IDN, u16 Position[], u16 Time[],
                              u16 Speed[]);        // Synchronously write multiple servo position commands
    virtual int PWMMode(u8 ID);                    // PWM output mode
    virtual int WritePWM(u8 ID, s16 pwmOut);       // PWM output mode command
    virtual int SwitchMode(int ID, uint8_t mode);  // Switch between position mode and PWM mode
    virtual int EnableTorque(u8 ID, u8 Enable);    // Torque control command
    virtual int unLockEprom(u8 ID);                // eprom unlock
    virtual int LockEprom(u8 ID);                  // eprom lock
    virtual int FeedBack(int ID);                  // Feedback servo information
    virtual int ReadPos(int ID);                   // Read position
    virtual int ReadSpeed(int ID);                 // Read speed
    virtual int ReadLoad(int ID);                  // Read the output voltage percentage to the motor (0~1000)
    virtual int ReadVoltage(int ID);               // Read voltage
    virtual int ReadTemper(int ID);                // Read temperature
    virtual int ReadMove(int ID);                  // Read movement status
    virtual int ReadCurrent(int ID);               // read current
    virtual int ReadMode(int ID);
    virtual int ReadToqueEnable(int ID);
    virtual int CalibrationOfs(u8 ID);
    virtual int ReadInfoValue(int ID, int AddInput);

private:
    u8 Mem[SCSCL_PRESENT_CURRENT_H - SCSCL_PRESENT_POSITION_L + 1];
    int min_angle[2];
    int max_angle[2];
};

#endif