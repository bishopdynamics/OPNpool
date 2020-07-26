/**
 * @brief Data Link layer: bytes from the RS485 transceiver to data packets
 *
 * CLOSED SOURCE, NOT FOR PUBLIC RELEASE
 * (c) Copyright 2020, Coert Vonk
 * All rights reserved. Use of copyright notice does not imply publication.
 * All text above must be included in any redistribution
 **/

#include <esp_system.h>
#include <esp_log.h>
#include <time.h>

#include "../rs485/rs485.h"
#include "../network/network.h"
#include "skb/skb.h"
#include "datalink.h"

static char const * const TAG = "datalink_rx";

static struct proto_info_t {
	uint8_t const * const preamble;
	uint const len;
    datalink_prot_t const prot;
	uint idx;
} _proto_infos[] = {
	{
        .preamble = datalink_preamble_ic,
        .len = sizeof(datalink_preamble_ic),
        .prot = DATALINK_PROT_IC,
    },
	{
        .preamble = datalink_preamble_a5,
        .len = sizeof(datalink_preamble_a5),
        .prot = DATALINK_PROT_A5_CTRL,  // distinction between A5_CTRL and A5_PUMP is based on src/dst in hdr
    },
};

typedef enum DATALINK_RESULT_t {
    DATALINK_RESULT_DONE,
    DATALINK_RESULT_INCOMPLETE,
    DATALINK_RESULT_ERROR,
} DATALINK_RESULT_t;

static void
_preamble_reset()
{
	for (uint ii = 0; ii < ARRAY_SIZE(_proto_infos); ii++) {
		_proto_infos[ii].idx = 0;
	}
}

static bool
_preamble_is_done(struct proto_info_t * const pi, uint8_t const b, bool * found)
{
	if (b == pi->preamble[pi->idx]) {
		*found = true;
		pi->idx++;
		if (pi->idx == pi->len) {
			_preamble_reset();
			return true;
		}
	} else {
		*found = false;
	}
	return false;
}

static DATALINK_RESULT_t
_find_preamble(rs485_handle_t const rs485, datalink_pkt_t * const pkt)
{
    uint len = 0;
    uint buf_size = 40;
    char dbg[buf_size];

	while (rs485->available()) {
		uint8_t byt = rs485->read();
		if (CONFIG_POOL_DBG_DATALINK) {
            len += snprintf(dbg + len, buf_size - len, " %02X", byt);
		}
		bool found = false;  // could be the next byte of a preamble
		for (uint pp = 0; !found && pp < ARRAY_SIZE(_proto_infos); pp++) {
			if (_preamble_is_done(&_proto_infos[pp], byt, &found)) {
				pkt->prot = _proto_infos[pp].prot;
				if (CONFIG_POOL_DBG_DATALINK) {
                    ESP_LOGI(TAG, "%s (preamble)", dbg);
				}
				return DATALINK_RESULT_DONE;
			}
		}
		if (!found) {  // or, could be the beginning of a preamble
			_preamble_reset();
			for (uint pp = 0; pp < ARRAY_SIZE(_proto_infos); pp++) {
				(void)_preamble_is_done(&_proto_infos[pp], byt, &found);
			}
		}
	}
	return DATALINK_RESULT_INCOMPLETE;
}

static DATALINK_RESULT_t
_read_hdr(rs485_handle_t const rs485, datalink_pkt_t * const pkt)
{
	switch (pkt->prot) {
		case DATALINK_PROT_A5_CTRL:
		case DATALINK_PROT_A5_PUMP: {
            datalink_a5_hdr_t * const hdr = &pkt->head->a5.hdr;
			if (rs485->available() >= (int)sizeof(datalink_a5_hdr_t)) {
				rs485->read_bytes((uint8_t *) hdr, sizeof(datalink_a5_hdr_t));
				if (CONFIG_POOL_DBG_DATALINK) {
					ESP_LOGI(TAG, " %02X %02X %02X %02X %02X (header)", hdr->ver, hdr->dst, hdr->src, hdr->typ, hdr->len);
				}
				if (hdr->len > DATALINK_MAX_DATA_SIZE) {
					return DATALINK_RESULT_ERROR;  // pkt length exceeds what we have planned for
				}
    			if ( (datalink_groupaddr(hdr->src) == DATALINK_ADDRGROUP_PUMP) || (datalink_groupaddr(hdr->dst) == DATALINK_ADDRGROUP_PUMP) ) {
                    pkt->prot = DATALINK_PROT_A5_PUMP;
                }
                pkt->data_len = hdr->len;
				return DATALINK_RESULT_DONE;
			}
			break;
        }
		case DATALINK_PROT_IC: {
            datalink_ic_hdr_t * const hdr = &pkt->head->ic.hdr;
			if (rs485->available() >= (int)sizeof(datalink_ic_hdr_t)) {
				rs485->read_bytes((uint8_t *) hdr, sizeof(datalink_ic_hdr_t));
				if (CONFIG_POOL_DBG_DATALINK) {
					ESP_LOGI(TAG, " %02X %02X (header)\n", hdr->dst, hdr->typ);
				}
                pkt->data_len = network_ic_len(hdr->typ);
				return DATALINK_RESULT_DONE;
			}
        }
	}
	return DATALINK_RESULT_INCOMPLETE;
}

static DATALINK_RESULT_t
_read_data(rs485_handle_t const rs485, datalink_pkt_t * const pkt)
{
    uint len = 0;
    uint buf_size = 80;
    char buf[buf_size];

	if (rs485->available() >= pkt->data_len) {
		rs485->read_bytes((uint8_t *) pkt->data, pkt->data_len);
		if (CONFIG_POOL_DBG_DATALINK) {
			for (uint ii = 0; ii < pkt->data_len; ii++) {
                len += snprintf(buf + len, buf_size - len, " %02X", pkt->data[ii]);
			}
            ESP_LOGI(TAG, "%s (data)", buf);
		}
		return DATALINK_RESULT_DONE;
	}
	return DATALINK_RESULT_INCOMPLETE;
}

static DATALINK_RESULT_t
_read_crc(rs485_handle_t const rs485, datalink_pkt_t * const pkt)
{
	switch (pkt->prot) {
		case DATALINK_PROT_A5_CTRL:
		case DATALINK_PROT_A5_PUMP: {
            uint8_t * const crc = pkt->tail->a5.crc;
			if (rs485->available() >= (int)sizeof(uint16_t)) {
                crc[0] = rs485->read();
                crc[1] = rs485->read();
				if (CONFIG_POOL_DBG_DATALINK) {
					ESP_LOGI(TAG, " %03X (checksum)\n", (uint16_t)crc[0] << 8 | crc[1]);
				}
				return DATALINK_RESULT_DONE;
			}
			break;
        }
		case DATALINK_PROT_IC: {
            uint8_t * const crc = pkt->tail->a5.crc;
			if (rs485->available() >= (int)sizeof(uint8_t)) {
				crc[0] = rs485->read();  // store it as uint16_t, so we can treat it as A5 pkt
				if (CONFIG_POOL_DBG_DATALINK) {
					ESP_LOGI(TAG, " %02X (checksum)\n", crc[0]);
				}
				return DATALINK_RESULT_DONE;
			}
			break;
        }
	}
	return DATALINK_RESULT_INCOMPLETE;
}

static bool
_crc_is_correct(datalink_pkt_t * pkt)
{
	switch (pkt->prot) {
		case DATALINK_PROT_A5_CTRL:
		case DATALINK_PROT_A5_PUMP: {
            uint8_t * const crc_start = &pkt->head->a5.preamble[sizeof(datalink_a5_preamble_t) - 1];  // starting at the last by of the preamble
            uint8_t * const crc_stop = pkt->data + pkt->data_len;
            uint16_t const crc = datalink_calc_crc(crc_start, crc_stop);
			return pkt->tail->a5.crc[0] == (crc >> 8) && pkt->tail->a5.crc[1] == (crc & 0xFF);
        }
		case DATALINK_PROT_IC: {
            uint8_t * const crc_start = pkt->head->ic.preamble;  // starting at the last by of the preamble
            uint8_t * const crc_stop = pkt->data + pkt->data_len;
            uint8_t const crc = datalink_calc_crc(crc_start, crc_stop) & 0xFF;
			return pkt->tail->a5.crc[0] == crc;
        }
	}
    return false;
}

uint32_t _millis() {
	return (uint32_t) (clock() * 1000 / CLOCKS_PER_SEC);
}

typedef enum STATE_t {
    STATE_FIND_PREAMBLE,
    STATE_READ_HDR,
    STATE_READ_DATA,
    STATE_READ_CRC,
    STATE_DONE,
} STATE_t;

static STATE_t _state = STATE_FIND_PREAMBLE;

void
_change_state(datalink_pkt_t * const pkt, STATE_t const state)
{
    _state = state;

    switch (state) {
		case STATE_FIND_PREAMBLE:
            skb_reset(pkt->skb);
            break;
		case STATE_READ_HDR:
            pkt->head = (datalink_head_t *) skb_put(pkt->skb, sizeof(datalink_head_t));
            break;
		case STATE_READ_DATA:
            pkt->data = (datalink_data_t *) skb_put(pkt->skb, pkt->data_len);
            break;
		case STATE_READ_CRC:
            pkt->tail = (datalink_tail_t *) skb_put(pkt->skb, sizeof(datalink_tail_t));
            break;
		case STATE_DONE:
            break;
    }
}

/**
 * Receive packets from the Pentair RS-485 bus.
 * Returns true when a complete packet has been received
 *
 * It relies on data from the network layer, for the correct length of the data pkt.
 */

bool
datalink_rx_pkt(rs485_handle_t const rs485, datalink_pkt_t * const pkt)
{
	time_t start = _millis();

	if (_state != STATE_FIND_PREAMBLE && _millis() - start > CONFIG_POOL_RS485_TIMEOUT) {
		ESP_LOGW(TAG, "timeout");
		_change_state(pkt, STATE_FIND_PREAMBLE);
	}
	DATALINK_RESULT_t result = DATALINK_RESULT_ERROR;  // init to please compiler
	switch (_state) {
		case STATE_FIND_PREAMBLE:
            result = _find_preamble(rs485, pkt);
            break;
		case STATE_READ_HDR:
            result = _read_hdr(rs485, pkt);
            break;
		case STATE_READ_DATA:
            result = _read_data(rs485, pkt);
            break;
		case STATE_READ_CRC:
            result = _read_crc(rs485, pkt);
            break;
		case STATE_DONE:
            break;
	}
	switch (result) {
		case DATALINK_RESULT_DONE:
			switch (_state) {
				case STATE_FIND_PREAMBLE:
					start = _millis();
					_change_state(pkt, STATE_READ_HDR);
					break;
				case STATE_READ_HDR:
					_change_state(pkt, STATE_READ_DATA);
					break;
				case STATE_READ_DATA:
					_change_state(pkt, STATE_READ_CRC);
					break;
				case STATE_READ_CRC:
					_change_state(pkt, STATE_DONE);
					break;
				case STATE_DONE:
					break;
			}
			break;
		case DATALINK_RESULT_INCOMPLETE:
			break;
		case DATALINK_RESULT_ERROR:
			_change_state(pkt, STATE_FIND_PREAMBLE);
			break;
	}

	if (_state == STATE_DONE) {


#if 0
                // // so we can treat it as a A5 message when reading data or while decoding
                hdr->ver = 0x00;
                hdr->dst = hdr_ic.dst;
                hdr->src = 0x00;
                hdr->typ = hdr_ic.typ;
                hdr->len = len;
#endif


		_change_state(pkt, STATE_FIND_PREAMBLE);
        datalink_addrgroup_t const dst_a5 = datalink_groupaddr(pkt->head->a5.hdr.dst);
        datalink_addrgroup_t const dst_ic = datalink_groupaddr(pkt->head->ic.hdr.dst);

        if ((pkt->prot == DATALINK_PROT_A5_CTRL && dst_a5 == DATALINK_ADDRGROUP_X09) ||
            (pkt->prot == DATALINK_PROT_IC && dst_ic != DATALINK_ADDRGROUP_ALL && dst_ic != DATALINK_ADDRGROUP_CHLOR)) {

                return false;  // silently ignore
        }
        return _crc_is_correct(pkt);
	}
	return false;
}
