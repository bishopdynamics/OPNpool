/**
* @brief conert pool state to JSON
 *
 * CLOSED SOURCE, NOT FOR PUBLIC RELEASE
 * (c) Copyright 2020, Coert Vonk
 * All rights reserved. Use of copyright notice does not imply publication.
 * All text above must be included in any redistribution
 **/

#include <string.h>
#include <esp_system.h>
#include <esp_log.h>
#include <cJSON.h>

#include "../network/network.h"
#include "../network/name.h"
#include "poolstate.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif

static char const * const TAG = "poolstate";

void
cPool_AddPumpPrgToObject(cJSON * const obj, char const * const key, uint16_t const value)
{
    cJSON_AddNumberToObject(obj, key, value);
}

void
cPool_AddPumpCtrlToObject(cJSON * const obj, char const * const key, uint8_t const ctrl)
{
    char const * str;
    switch (ctrl) {
        case 0x00: str = "local"; break;
        case 0xFF: str = "remote"; break;
        default: str = name_hex8(ctrl);
    }
    cJSON_AddStringToObject(obj, key, str);
}

void
cPool_AddPumpModeToObject(cJSON * const obj, char const * const key, uint8_t const mode)
{
    cJSON_AddStringToObject(obj, key, name_pump_mode(mode));
}

void
cPool_AddFirmwareToObject(cJSON * const obj, char const * const key, poolstate_version_t const * const version)
{
    float const fw = version->major + (float)version->minor / 100;
    cJSON_AddNumberToObject(obj, key, fw);
}

void
cPool_AddThermostatToObject(cJSON * const obj, char const * const key, poolstate_thermostat_t const * const thermostat)
{
    cJSON * const item = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, key, item);
    cJSON_AddNumberToObject(item, "temp", thermostat->temp);
    cJSON_AddStringToObject(item, "src", name_heat_src(thermostat->heat_src));
    if (thermostat->heating) {
        cJSON_AddTrueToObject(item, "heating");
    } else {
        cJSON_AddFalseToObject(item, "heating");
    }
}

void
cPool_AddTempToObject(cJSON * const obj, char const * const key, uint8_t const temp)
{
    if (temp != 0xFF) {
        cJSON_AddNumberToObject(obj, key, temp);
    }
}

void
cPool_AddActiveCircuitsToObject(cJSON * const obj, char const * const key, uint16_t const active)
{
    uint active_len = 0;
    char const * active_names[16];
    uint16_t mask = 0x00001;
    for (uint ii = 0; mask; ii++) {
        if (active & mask) {
            active_names[active_len++] = name_circuit(ii + 1);
        }
        mask <<= 1;
    }
    cJSON * const item = cJSON_CreateStringArray(active_names, active_len);
    cJSON_AddItemToObject(obj, key, item);
}

void
cPool_AddSchedToObject(cJSON * const obj, char const * const key, uint16_t const start, uint16_t const stop)
{
    cJSON * const item = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, key, item);
    cJSON_AddStringToObject(item, "start", name_time(start / 60, start % 60));
    cJSON_AddStringToObject(item, "stop", name_time(stop / 60, stop % 60));
}

void
cPool_AddPumpRunningToObject(cJSON * const obj, char const * const key, bool const pump_state_running)
{
    if (pump_state_running) {
        cJSON_AddTrueToObject(obj, key);
    } else {
        cJSON_AddFalseToObject(obj, key);
    }
}

void
cPool_AddDateToObject(cJSON * const obj, char const * const key, poolstate_date_t const * const date)
{
    cJSON_AddStringToObject(obj, key, name_date(date->year, date->month, date->day));
}

void
cPool_AddTimeToObject(cJSON * const obj, char const * const key, poolstate_time_t const * const time)
{
    cJSON_AddStringToObject(obj, key, name_time(time->hour, time->minute));
}

void
cPool_AddPumpStatusToObject(cJSON * const obj, char const * const key, poolstate_t const * const state)
{
    cJSON * const item = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, key, item);
    cPool_AddPumpRunningToObject(item, "running", state->pump.running);
    cPool_AddPumpModeToObject(item, "mode", state->pump.mode);
    cJSON_AddNumberToObject(item, "status", state->pump.status);
    cJSON_AddNumberToObject(item, "pwr", state->pump.pwr);
    cJSON_AddNumberToObject(item, "rpm", state->pump.rpm);
    if (state->pump.gpm) {
        cJSON_AddNumberToObject(item, "gpm", state->pump.gpm);
    }
    if (state->pump.pct) {
        cJSON_AddNumberToObject(item, "pct", state->pump.pct);
    }
    cJSON_AddNumberToObject(item, "err", state->pump.mode);
    cJSON_AddNumberToObject(item, "timer", state->pump.mode);
    cPool_AddTimeToObject(item, "time", &state->pump.time);
}

void
cPool_AddTodToObject(cJSON * const obj, char const * const key, poolstate_tod_t const * const state_tod)
{
    cJSON * const item = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, key, item);
    cPool_AddTimeToObject(item, "time", &state_tod->time);
    cPool_AddDateToObject(item, "date", &state_tod->date);
}

void
cPool_AddStateToObject(cJSON * const obj, poolstate_t const * const state)
{
    cPool_AddTimeToObject(obj, "time", &state->tod.time);
    cPool_AddFirmwareToObject(obj, "firmware", &state->version);
    cPool_AddActiveCircuitsToObject(obj, "active", state->circuits.active);
    cPool_AddThermostatToObject(obj, "pool", &state->pool);
    cPool_AddThermostatToObject(obj, "spa", &state->spa);
    cPool_AddTempToObject(obj, "air", state->air.temp);
    cPool_AddTempToObject(obj, "solar", state->solar.temp);
}

size_t
state_to_json(poolstate_t * state, char * buf, size_t buf_len)
{
	name_reset_idx();

    cJSON * const obj = cJSON_CreateObject();
    cPool_AddStateToObject(obj, state);

    assert( cJSON_PrintPreallocated(obj, buf, buf_len, false) );
	cJSON_Delete(obj);

	ESP_LOGI(TAG, "%s", buf);
	return strlen(buf);
}