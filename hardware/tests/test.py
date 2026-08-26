# -*- coding: utf-8 -*-
"""
SCS0009 舵机最小测试程序
功能：分别控制 ID=1 和 ID=2 舵机，支持前进、停止、回中、关闭扭矩
"""
import serial
import time

# ====== 硬件参数 ======
PORT = 'COM7'
BAUD = 1000000

# ====== 协议常量 ======
INST_READ  = 0x02
INST_WRITE = 0x03
INST_STOP  = 0x07

# ====== 内存地址 ======
ADDR_TORQUE    = 40   # 扭矩使能 (1B, RW)
ADDR_ERROR     = 41   # 错误状态 (1B, RO)
ADDR_GOAL_POS  = 42   # 目标位置 (2B, RW, 大端)
ADDR_RUN_SPEED = 46   # 运行速度 (2B, RW, 大端)
ADDR_CUR_POS   = 56   # 当前位置 (2B, RO, 大端)

# ====== 运动参数 ======
POS_MIN = 20
POS_MAX = 1003
STEP    = 50
SPEED   = 500
DWELL   = 5

# ====== 舵机配置 ======
SERVOS = [
    {'id': 1, 'name': 'ID=1'},
    {'id': 2, 'name': 'ID=2'},
]

# ==================== 底层通信 ====================

def build_read(sid, addr, cnt):
    """构建读指令帧"""
    length = 4
    body = [0xFF, 0xFF, sid, length, INST_READ, addr & 0xFF, cnt & 0xFF]
    chk = ~(sid + length + INST_READ + (addr & 0xFF) + (cnt & 0xFF)) & 0xFF
    body.append(chk)
    return bytes(body)

def build_write(sid, addr, data):
    """构建写指令帧"""
    length = 3 + len(data)
    body = [0xFF, 0xFF, sid, length, INST_WRITE, addr & 0xFF] + data
    s = sid + length + INST_WRITE + (addr & 0xFF) + sum(data)
    chk = (~s) & 0xFF
    body.append(chk)
    return bytes(body)

def build_stop(sid):
    """构建停止指令帧：FF FF ID 02 07 CHK"""
    body = [0xFF, 0xFF, sid, 0x02, INST_STOP]
    chk = ~(sid + 0x02 + INST_STOP) & 0xFF
    body.append(chk)
    return bytes(body)

def write_reg(ser, sid, addr, data):
    """写寄存器（SCS 应答级别=1 时无返回）"""
    ser.write(build_write(sid, addr, data))
    time.sleep(0.02)
    return 0

def read_pos(ser, sid):
    """读取当前位置，返回 int 或 None"""
    ser.flushInput()
    ser.write(build_read(sid, ADDR_CUR_POS, 2))
    time.sleep(0.03)
    resp = ser.read(7)
    if len(resp) >= 7 and resp[0] == 0xFF and resp[1] == 0xFF:
        err = resp[4]
        if err == 0:
            return (resp[5] << 8) | resp[6]
    return None

# ==================== 控制接口 ====================

def enable_torque(ser, sid, on=True):
    """使能/关闭扭矩"""
    val = 1 if on else 0
    write_reg(ser, sid, ADDR_TORQUE, [val])

def set_speed(ser, sid, speed):
    """设置运行速度（大端序）"""
    write_reg(ser, sid, ADDR_RUN_SPEED, [(speed >> 8) & 0xFF, speed & 0xFF])

def move_to(ser, sid, pos):
    """移动到目标位置（大端序，自动钳位到安全范围）"""
    pos = max(POS_MIN, min(POS_MAX, pos))
    write_reg(ser, sid, ADDR_GOAL_POS, [(pos >> 8) & 0xFF, pos & 0xFF])

def stop_servo(ser, sid):
    """立即停止舵机（保持扭矩，锁在当前位置）"""
    ser.write(build_stop(sid))
    time.sleep(0.02)

def get_error(ser, sid):
    """读取错误状态（0=正常）"""
    ser.flushInput()
    ser.write(build_read(sid, ADDR_ERROR, 1))
    time.sleep(0.02)
    resp = ser.read(6)
    if len(resp) >= 6 and resp[4] == 0:
        return resp[5]
    return -1

def wait_arrive(ser, sid, target, tolerance=3, timeout=3.0):
    """等待舵机到达目标位置，返回 True/False"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        p = read_pos(ser, sid)
        if p is not None and abs(p - target) <= tolerance:
            return True
        time.sleep(0.05)
    return False

# ==================== 测试流程 ====================

def main():
    print(f"✅ 串口 {PORT} @ {BAUD}bps\n")

    with serial.Serial(PORT, BAUD, timeout=0.5) as ser:

        # 步骤1：使能扭矩
        print(">>> 步骤1：使能扭矩")
        for s in SERVOS:
            enable_torque(ser, s['id'], True)
            print(f"    {s['name']} 扭矩已使能")
        time.sleep(0.3)

        # 步骤2：记录初始位置（回中目标）
        print("\n>>> 步骤2：记录初始位置")
        home_pos = {}
        for s in SERVOS:
            p = read_pos(ser, s['id'])
            home_pos[s['id']] = p
            print(f"    {s['name']} 初始位置 = {p}")

        # 步骤3：前进
        print(f"\n>>> 步骤3：前进（速度={SPEED}，步长={STEP}）")
        target_pos = {}
        for s in SERVOS:
            cur = read_pos(ser, s['id'])
            tgt = cur + STEP
            set_speed(ser, s['id'], SPEED)
            time.sleep(0.02)
            move_to(ser, s['id'], tgt)
            target_pos[s['id']] = tgt
            print(f"    {s['name']}: {cur} → {tgt}")

        # 等待到位
        for s in SERVOS:
            ok = wait_arrive(ser, s['id'], target_pos[s['id']])
            status = "到位 ✅" if ok else "超时 ⚠️"
            print(f"    {s['name']} {status}")

        # 步骤4：停留观察（每 0.5 秒打印位置）
        print(f"\n>>> 步骤4：停留观察 {DWELL} 秒")
        for i in range(DWELL * 2):
            time.sleep(0.5)
            positions = [f"{s['name']}={read_pos(ser, s['id'])}" for s in SERVOS]
            print(f"    [{(i+1)*0.5:.1f}s] {' | '.join(positions)}")

        # 步骤5：回中
        print("\n>>> 步骤5：自动回中")
        for s in SERVOS:
            set_speed(ser, s['id'], SPEED)
            time.sleep(0.02)
            move_to(ser, s['id'], home_pos[s['id']])
            print(f"    {s['name']} → {home_pos[s['id']]}")

        for s in SERVOS:
            ok = wait_arrive(ser, s['id'], home_pos[s['id']])
            status = "已回中 ✅" if ok else "回中失败 ⚠️"
            print(f"    {s['name']} {status}")

        # 步骤6：停止（保持扭矩，锁住位置）
        print("\n>>> 步骤6：停止舵机（保持扭矩）")
        for s in SERVOS:
            stop_servo(ser, s['id'])
            print(f"    {s['name']} 已停止")

        # 步骤7：错误检查
        print("\n>>> 步骤7：错误状态检查")
        for s in SERVOS:
            err = get_error(ser, s['id'])
            print(f"    {s['name']} 错误码: {err}")

        # 步骤8：关闭扭矩
        print("\n>>> 步骤8：关闭扭矩，舵机进入自由状态")
        for s in SERVOS:
            enable_torque(ser, s['id'], False)
            print(f"    {s['name']} 扭矩已关闭")

    print("\n🎉 测试完成！")

if __name__ == '__main__':
    main()