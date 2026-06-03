/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

/* Attributes State Machine for SMART_Organ profile */
typedef enum {
    IDX_SVC = 0,
    IDX_CHAR_RW,
    IDX_CHAR_VAL_RW,
    IDX_CHAR_NTF,
    IDX_CHAR_VAL_NTF,
    IDX_CHAR_CFG_NTF,
    HRS_IDX_NB
} hrs_idx_e;
