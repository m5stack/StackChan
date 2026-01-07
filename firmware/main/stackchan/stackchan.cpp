/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stackchan.h"

stackchan::StackChan& GetStackChan()
{
    static stackchan::StackChan stackchan;
    return stackchan;
}
