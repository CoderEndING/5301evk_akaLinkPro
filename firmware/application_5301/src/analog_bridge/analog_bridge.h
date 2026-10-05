/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ANALOG_BRIDGE_H
#define ANALOG_BRIDGE_H
#include <stdint.h>
#define ANALOG_CMD 0x38U
void analog_bridge_hid(uint8_t *req, uint8_t *res);
uint8_t analog_periodic_ready(void);
uint8_t analog_periodic_check(const uint8_t *p, uint16_t len);
uint8_t analog_periodic_exec(const uint8_t *p, uint16_t len, uint8_t *data, uint8_t *n);
#endif
