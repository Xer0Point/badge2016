/*
 * Copyright (c) 2016, Conor Patrick
 * Copyright (c) 2016, Karl Koscher
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the FreeBSD Project.
 *
 * atecc508.c
 * 		Implementation for ATECC508 peripheral.
 *
 */
#include <stdint.h>
#include <stdlib.h>
#include <util/delay.h>
#include "blinkybadge.h"
#include "atecc508a.h"
#include "twi.h"

int8_t atecc_send(uint8_t cmd, uint8_t p1, uint16_t p2,
					uint8_t * buf, uint8_t len)
{
	static uint8_t params[6];
	params[0] = 0x3;
	params[1] = 7+len;
	params[2] = cmd;
	params[3] = p1;
	params[4] = ((uint8_t*)&p2)[1];
	params[5] = ((uint8_t* )&p2)[0];

	if (!twiSendExtPkt(ATECC508A_ADDR, params, sizeof(params), buf, len)) {
		return -1;
	}
	
	return 0;
}

void atecc_idle()
{
	twiSendPkt(ATECC508A_ADDR, "\x02", 1);
}

void atecc_sleep()
{
	twiSendPkt(ATECC508A_ADDR, "\x01", 1);
}

void atecc_wake()
{
	twiSendPkt(ATECC508A_ADDR, "\0\0", 2);
}

#define PKT_CRC(buf, pkt_len) (htole16(*((uint16_t*)(buf+pkt_len-2))))

int8_t atecc_recv(uint8_t * buf, uint8_t buflen, struct atecc_response* res)
{
	uint8_t pkt_len;
	pkt_len = twiRecvVariableLenPkt(ATECC508A_ADDR, buf, buflen);
	if (pkt_len == 0)
	{
		return -1;
	}

	if (pkt_len <= buflen && pkt_len >= 4)
	{
		/* FIXME
		if (PKT_CRC(buf,pkt_len) != SMB_crc)
		{
			set_app_error(ERROR_I2C_CRC);
			return -1;
		}
		*/
	}
	else
	{
		set_app_error(ERROR_I2C_BAD_LEN);
		return -1;
	}

	if (pkt_len == 4 && buf[1] != 0)
	{
		set_app_error(buf[1]);
		return -1;
	}

	if (res != NULL)
	{
		res->len = pkt_len - 3;
		res->buf = buf+1;
	}
	return pkt_len;
}

static void delay_cmd(uint8_t cmd)
{
	uint8_t d = 0;
	switch(cmd)
	{
		case ATECC_CMD_SIGN:
			d = 50;
			break;
		case ATECC_CMD_GENKEY:
			d = 100;
			break;
		default:
			d = 32;
			break;
	}
	u2f_delay(d);
}

int8_t atecc_send_recv(uint8_t cmd, uint8_t p1, uint16_t p2,
							uint8_t* tx, uint8_t txlen, uint8_t * rx,
							uint8_t rxlen, struct atecc_response* res)
{
	uint8_t errors = 0;
	atecc_wake();
	resend:
	while(atecc_send(cmd, p1, p2, tx, txlen) == -1)
	{
		u2f_delay(10);
		errors++;
		if (errors > 8)
		{
			return -1;
		}
	}
	while(atecc_recv(rx,rxlen, res) == -1)
	{
		errors++;
		if (errors > 5)
		{
			return -2;
		}
		switch(get_app_error())
		{
			case ERROR_NOTHING:
				delay_cmd(cmd);
				break;
			default:
				u2f_delay(cmd);
				goto resend;
				break;
		}

	}
	atecc_idle();
	return 0;
}

int8_t atecc_write_eeprom(uint8_t base, uint8_t offset, uint8_t* srcbuf, uint8_t len)
{
	uint8_t buf[7];
	struct atecc_response res;

	uint8_t * dstbuf = srcbuf;
	if (offset + len > 4)
		return -1;
	if (len < 4)
	{
		atecc_send_recv(ATECC_CMD_READ,
				ATECC_RW_CONFIG, base, NULL, 0,
				buf, sizeof(buf), &res);

		dstbuf = res.buf;
		memmove(res.buf + offset, srcbuf, len);
	}

	atecc_send_recv(ATECC_CMD_WRITE,
			ATECC_RW_CONFIG, base, dstbuf, 4,
			buf, sizeof(buf), &res);

	if (res.buf[0])
	{
		set_app_error(-res.buf[0]);
		return -1;
	}
	return 0;
}

#ifdef ATECC_SETUP_DEVICE

static int is_locked(uint8_t * buf)
{
	struct atecc_response res;
	atecc_send_recv(ATECC_CMD_READ,
					ATECC_RW_CONFIG,87/4, NULL, 0,
					buf, 36, &res);
	//dump_hex(res.buf, res.len);
	if (res.buf[87 % 4] == 0)
		return 1;
	else
		return 0;
}


static void dump_config(uint8_t* buf)
{
	uint8_t i,j;
	//uint16_t crc = 0;
	struct atecc_response res;
	for (i=0; i < 4; i++)
	{
		atecc_send_recv(ATECC_CMD_READ,
				ATECC_RW_CONFIG | ATECC_RW_EXT, i << 3, NULL, 0,
				buf, 40, &res);
		/*for(j = 0; j < res.len; j++)
		{
			crc = feed_crc(crc,res.buf[j]);
		}
		*/
		//dump_hex(res.buf,res.len);
	}

	//u2f_printx("config crc:", 1,reverse_bits(crc));
}

static void atecc_setup_config(uint8_t* buf)
{
	struct atecc_response res;
	uint8_t i;

	struct atecc_slot_config sc;
	struct atecc_key_config kc;
	memset(&sc, 0, sizeof(struct atecc_slot_config));
	memset(&kc, 0, sizeof(struct atecc_key_config));
	sc.readkey = 3;
	sc.secret = 1;
	sc.writeconfig = 0xa;

	// set up read/write permissions for keys
	for (i = 0; i < 16; i++)
	{
		if ( atecc_write_eeprom(ATECC_EEPROM_SLOT(i), ATECC_EEPROM_SLOT_OFFSET(i), &sc, ATECC_EEPROM_SLOT_SIZE) != 0)
		{
			//u2f_printb("1 atecc_write_eeprom failed ",1, i);
		}

	}


	kc.private = 1;
	kc.pubinfo = 1;
	kc.keytype = 0x4;
	kc.lockable = 0;

	// set up config for keys
	for (i = 0; i < 16; i++)
	{
		if (i==15)
		{
			kc.lockable = 1;
		}
		if ( atecc_write_eeprom(ATECC_EEPROM_KEY(i), ATECC_EEPROM_KEY_OFFSET(i), &kc, ATECC_EEPROM_KEY_SIZE) != 0)
		{
			//u2f_printb("3 atecc_write_eeprom failed " ,1,i);
		}

	}

	dump_config(buf);
}

// write a message to the otp memory before locking
static void atecc_write_otp(uint8_t * buf)
{
	char msg[] = "conorpp's u2f token.\r\n\0\0\0\0";
	int i;
	for (i=0; i<sizeof(msg); i+=4)
	{
		atecc_send_recv(ATECC_CMD_WRITE,
				ATECC_RW_OTP, ATECC_EEPROM_B2A(i), msg+i, 4,
				buf, sizeof(buf), NULL);
	}
}

void atecc_setup_init(uint8_t * buf)
{
	// 13s watchdog
	//WDTCN = 7;
	if (!is_locked(buf))
	{
		//u2f_prints("setting up config...\r\n");
		atecc_setup_config(buf);
	}
	else
	{
		//u2f_prints("already locked\r\n");
	}
}

// buf should be at least 40 bytes
void atecc_setup_device(struct config_msg * msg)
{
	struct atecc_response res;
	struct config_msg usbres;

	static uint16_t crc = 0;
	uint8_t buf[40];

	memset(&usbres, 0, sizeof(struct config_msg));
	usbres.cmd = msg->cmd;

	switch(msg->cmd)
	{
		case U2F_CONFIG_GET_SERIAL_NUM:

			//u2f_prints("U2F_CONFIG_GET_SERIAL_NUM\r\n");
			atecc_send_recv(ATECC_CMD_READ,
					ATECC_RW_CONFIG | ATECC_RW_EXT, 0, NULL, 0,
					buf, 40, &res);
			memmove(usbres.buf+1, res.buf, 15);
			usbres.buf[0] = 15;
			break;

		case U2F_CONFIG_IS_BUILD:
			//u2f_prints("U2F_CONFIG_IS_BUILD\r\n");
			usbres.buf[0] = 1;
			break;
		case U2F_CONFIG_IS_CONFIGURED:
			//u2f_prints("U2F_CONFIG_IS_CONFIGURED\r\n");
			usbres.buf[0] = 1;
			break;
		case U2F_CONFIG_LOCK:
			crc = *(uint16_t*)msg->buf;
			usbres.buf[0] = 1;
			//u2f_printx("got crc: ",1,crc);

			if (!is_locked(buf))
			{
				if (atecc_send_recv(ATECC_CMD_LOCK,
						ATECC_LOCK_CONFIG, crc, NULL, 0,
						buf, sizeof(buf), NULL))
				{
					//u2f_prints("ATECC_CMD_LOCK failed\r\n");
					return;
				}
			}
			else
			{
				//u2f_prints("already locked\r\n");
			}
			break;
		case U2F_CONFIG_GENKEY:
			//u2f_prints("U2F_CONFIG_GENKEY\r\n");

			atecc_send_recv(ATECC_CMD_GENKEY,
					ATECC_GENKEY_PRIVATE, U2F_ATTESTATION_KEY_SLOT, NULL, 0,
					appdata.tmp, sizeof(appdata.tmp), &res);

			//u2f_printb("key is bytes ",1,res.len);

			memmove((uint8_t*)&usbres, res.buf, 64);

			break;
		default:
			//u2f_printb("invalid command: ",1,msg->cmd);
	}

	usb_write((uint8_t*)&usbres, FIDO_U2F_EPSIZE);

}
#endif