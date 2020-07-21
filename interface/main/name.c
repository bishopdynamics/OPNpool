/**
 * @brief Pool state to string
 *
 * CLOSED SOURCE, NOT FOR PUBLIC RELEASE
 * (c) Copyright 2015 - 2020, Coert Vonk
 * All rights reserved. Use of copyright notice does not imply publication.
 * All text above must be included in any redistribution
 **/

#include <string.h>
#include <esp_log.h>

#include "state_names.h"

// also used to display date/time, add +50
#define K_STR_BUF_SIZE ((sizeof(mHdr_a5_t) + sizeof(mCtrlState_a5_t) + 1) * 3 + 50)

// reusable global string
static struct str_t {
	char str[K_STR_BUF_SIZE];  // 3 bytes for each hex value when displaying raw; this is str.str[]
	uint_least8_t idx;
	char const * const noMem;
	char const * const digits;
} _str = {
	{},
	0,
	"sNOMEM", // increase size of str.str[]
	"0123456789ABCDEF"
};

void
name_resetIdx(void) {
	_str.idx = 0;
}

char const *
name_dateStr(uint8_t const year, uint8_t const month, uint8_t const day)
{
	uint_least8_t const nrdigits = 10; // 2015-12-31

	if (_str.idx + nrdigits + 1U >= ARRAY_SIZE(_str.str)) {
		return _str.noMem;  // increase size of str.str[]
	}
	char * s = _str.str + _str.idx;
	s[0] = '2'; s[1] = '0';
	s[2] = _str.digits[year / 10];
	s[3] = _str.digits[year % 10];
	s[4] = s[7] = '-';
	s[5] = _str.digits[month / 10];
	s[6] = _str.digits[month % 10];
	s[8] = _str.digits[day / 10];
	s[9] = _str.digits[day % 10];
	s[nrdigits] = '\0';
	_str.idx += nrdigits + 1U;
	return s;
}

char const *
name_timeStr(uint8_t const hours, uint8_t const minutes)
{
	uint_least8_t const nrdigits = 5;  // 00:00

	if (_str.idx + nrdigits + 1U >= ARRAY_SIZE(_str.str)) {
		return _str.noMem;  // increase size of str.str[]
	}
	char * s = _str.str + _str.idx;
	s[0] = _str.digits[hours / 10];
	s[1] = _str.digits[hours % 10];
	s[2] = ':';
	s[3] = _str.digits[minutes / 10];
	s[4] = _str.digits[minutes % 10];
	s[nrdigits] = '\0';
	_str.idx += nrdigits + 1U;
	return s;
}

/**
 * convert enum to string
 */

char const *
name_mtCtrlName(MT_CTRL_a5_t const mt, bool * const found)
{
	char const * s = NULL;
	switch (mt) {
		case MT_CTRL_setAck:     s = "setAck";     break;
		case MT_CTRL_circuitSet: s = "circuitSet"; break;
		case MT_CTRL_state:      s = "state";      break;
		case MT_CTRL_stateSet:   s = "stateSet";   break;
		case MT_CTRL_stateReq:   s = "stateReq";   break;
		case MT_CTRL_time:       s = "time";       break;
		case MT_CTRL_timeSet:    s = "timeSet";    break;
		case MT_CTRL_timeReq:    s = "timeReq";    break;
		case MT_CTRL_heat:       s = "heat";       break;
		case MT_CTRL_heatSet:    s = "heatSet";    break;
		case MT_CTRL_heatReq:    s = "heatReq";    break;
		case MT_CTRL_sched:      s = "sched";      break;
			// case MT_CTRL_schedSet:   s = "schedSet";   break;
		case MT_CTRL_schedReq:   s = "schedReq";   break;
#if 0
		case MT_CTRL_layout:     s = "layout";     break;
		case MT_CTRL_layoutSet:  s = "layoutSet";  break;
		case MT_CTRL_layoutReq:  s = "layoutReq";  break;
#endif
	}
	if (found) {
		*found = (s != NULL);
	}
	return s;
}

char const *
name_mtPumpName(MT_PUMP_a5_t const mt, bool const request, bool * const found)
{
	char const * s = NULL;
	char const * const ff = "FF";
	if (request) {
		switch (mt) {
			case MT_PUMP_regulate: s = "setReguReq";  break;
			case MT_PUMP_control:  s = "setCtrlReq";  break;
			case MT_PUMP_mode:     s = "setModeReq";  break;
			case MT_PUMP_state:    s = "setStateReq"; break;
			case MT_PUMP_status:   s = "statusReq";   break;
			case MT_PUMP_0xFF:     s = ff;            break;
		}
	} else {
		switch (mt) {
			case MT_PUMP_regulate: s = "setReguResp";  break;
			case MT_PUMP_control:  s = "setCtrlResp";  break;
			case MT_PUMP_mode:     s = "setModeResp";  break;
			case MT_PUMP_state:    s = "setStateResp"; break;
			case MT_PUMP_status:   s = "status";       break;
			case MT_PUMP_0xFF:     s = ff;             break;
		}
	}
	if (found) {
		*found = (s != NULL);
	}
	return s;
}

char const *
name_mtChlorName(MT_CHLOR_ic_t const mt, bool * const found)
{
	char const * s = NULL;
	switch (mt) {
		case MT_CHLOR_pingReq:    s = "pingReq";    break;
		case MT_CHLOR_ping:       s = "ping";       break;
		case MT_CHLOR_name:       s = "name";       break;
		case MT_CHLOR_lvlSet:     s = "lvlSet";     break;
		case MT_CHLOR_lvlSetResp: s = "lvlSetResp"; break;
		case MT_CHLOR_0x14:       s = "14";         break;
	}
	if (found) {
		*found = (s != NULL);
	}
	return s;
}

char const *
name_hex8str(uint8_t const value)
{
	uint_least8_t const nrdigits = sizeof(value) << 1;

	if (_str.idx + nrdigits + 1U >= ARRAY_SIZE(_str.str)) {
		return _str.noMem;  // increase size of str.str[]
	}
	char * s = _str.str + _str.idx;
	s[0] = _str.digits[(value & 0xF0) >> 4];
	s[1] = _str.digits[(value & 0x0F)];
	s[nrdigits] = '\0';
	_str.idx += nrdigits + 1U;
	return s;
}

char const *
name_hex16Str(uint16_t const value)
{
	uint_least8_t const nrdigits = sizeof(value) << 1;

	if (_str.idx + nrdigits + 1U >= ARRAY_SIZE(_str.str)) {
		return _str.noMem;  // increase size of str.str[]
	}
	char * s = _str.str + _str.idx;
	s[0] = _str.digits[(value & 0xF000) >> 12];
	s[1] = _str.digits[(value & 0x0F00) >> 8];
	s[2] = _str.digits[(value & 0x00F0) >> 4];
	s[3] = _str.digits[(value & 0x000F) >> 0];
	s[nrdigits] = '\0';
	_str.idx += nrdigits + 1U;
	return s;
}

char const *
name_strName(char const * const name, uint_least8_t const len)
{
	if (_str.idx + len + 1U >= ARRAY_SIZE(_str.str)) {
		return _str.noMem;  // increase size of str.str[]
	}
	char * s = _str.str + _str.idx;

	for (uint_least8_t ii = 0; ii < len; ii++) {
		s[ii] = name[ii];
	}
	s[len] = '\0';
	_str.idx += len + 1U;
	return s;
}

static char const * const kCircuitNames[] = {
	"spa", "aux1", "aux2", "aux3", "ft1", "pool", "ft1", "ft2", "ft3", "ft4"
};

char const *
name_circuitName(uint8_t const circuit)
{
	if (circuit > 0 && circuit <= ARRAY_SIZE(kCircuitNames)) {
		return kCircuitNames[circuit - 1];
	}
	if (circuit == 0x85) {
		return "heatBoost";
	}
	return strHex8(circuit);
}

static char const * const kChlorStateNames[] = {
	"ok", "highSalt", "lowSalt", "veryLowSalt", "lowFlow"
};

char const *
name_chlorStateName(uint8_t const chlorstate)
{
	if (chlorstate < ARRAY_SIZE(kChlorStateNames)) {
		return kChlorStateNames[chlorstate];
	}
	return strHex8(chlorstate);
}

uint_least8_t   // 1-based
name_circuitNr(char const * const name)
{
	for (uint_least8_t ii = 0; ii < ARRAY_SIZE(kCircuitNames); ii++) {
		if (strcmp(name, kCircuitNames[ii]) == 0) {
			return ii + 1;
		}
	}
	return 0;
}

static char const * const kHeatSrcNames[] = {
	"none", "heater", "solarPref", "solar"
};

char const *
name_heatSrcStr(uint8_t const value)
{
	if (value < ARRAY_SIZE(kHeatSrcNames)) {
		return kHeatSrcNames[value];
	}
	return strHex8(value);
}

uint_least8_t
name_heatsrcNr(char const * const name)
{
	for (uint_least8_t ii = 0; ii < ARRAY_SIZE(kHeatSrcNames); ii++) {
		if (strcmp(name, kHeatSrcNames[ii]) == 0) {
			return ii;
		}
	}
	return 0;
}

static char const * const kPumpModeNames[] = {
	"filter", "man", "bkwash", "3", "4", "5",
	"ft1", "7", "8", "ep1", "ep2", "ep3", "ep4"
};

char const *
name_pumptModeStr(uint16_t const value)
{
	if (value < ARRAY_SIZE(kPumpModeNames)) {
		return kPumpModeNames[value];
	}
	return strHex8(value);
}

char const *
name_pumpNamePrgStr(uint16_t const address)
{
	char const * s;
	switch (address) {
		case 0x2BF0: s = "?"; break;
		case 0x02E4: s = "pgm"; break;    // program GPM
		case 0x02C4: s = "rpm"; break;    // program RPM
#if 0
		case 0x0321: s = "eprg"; break;  // select ext prog, 0x0000=P0, 0x0008=P1, 0x0010=P2,
										 //                  0x0080=P3, 0x0020=P4
		case 0x0327: s = "eRpm0"; break; // program ext program RPM0
		case 0x0328: s = "eRpm1"; break; // program ext program RPM1
		case 0x0329: s = "eRpm2"; break; // program ext program RPM2
		case 0x032A: s = "eRpm3"; break; // program ext program RPM3
#endif
		default: s = Utils::strHex16(address);
	}
	return s;
}

char const * const kHeatSrcNames[] = {
	"none", "heater", "solarPref", "solar"
};

char const *
_heatSrcStr(uint8_t const value)
{
	if (value < ARRAY_SIZE(kHeatSrcNames)) {
		return kHeatSrcNames[value];
	}
	return strHex8(value);
}

uint_least8_t
_heatSrcNr(char const * const name)
{
	for (uint_least8_t ii = 0; ii < ARRAY_SIZE(kHeatSrcNames); ii++) {
		if (strcmp(name, kHeatSrcNames[ii]) == 0) {
			return ii;
		}
	}
	return 0;
}

static char const * const kPumpModeNames[] = {
	"filter", "man", "bkwash", "3", "4", "5",
	"ft1", "7", "8", "ep1", "ep2", "ep3", "ep4"
};

static char const *
_pumpModeStr(uint16_t const value)
{
	if (value < ARRAY_SIZE(kPumpModeNames)) {
		return kPumpModeNames[value];
	}
	return strHex8(value);
}

static char const *
_strPumpPrgName(uint16_t const address)
{
	char const * s;
	switch (address) {
		case 0x2BF0: s = "?"; break;
		case 0x02E4: s = "pgm"; break;    // program GPM
		case 0x02C4: s = "rpm"; break;    // program RPM
#if 0
		case 0x0321: s = "eprg"; break;  // select ext prog, 0x0000=P0, 0x0008=P1, 0x0010=P2,
										 //                  0x0080=P3, 0x0020=P4
		case 0x0327: s = "eRpm0"; break; // program ext program RPM0
		case 0x0328: s = "eRpm1"; break; // program ext program RPM1
		case 0x0329: s = "eRpm2"; break; // program ext program RPM2
		case 0x032A: s = "eRpm3"; break; // program ext program RPM3
#endif
		default: s = _hex16Str(address);
	}
	return s;
}

char const * const kCircuitNames[] = {
	"spa", "aux1", "aux2", "aux3", "ft1", "pool", "ft1", "ft2", "ft3", "ft4"
};

char const *
name_circuit(uint8_t const circuit)
{
	if (circuit > 0 && circuit <= ARRAY_SIZE(kCircuitNames)) {
		return kCircuitNames[circuit - 1];
	}
	if (circuit == 0x85) {
		return "heatBoost";
	}
	return strHex8(circuit);
}
