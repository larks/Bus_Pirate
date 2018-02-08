/*
 * This file is part of the Bus Pirate project (http://code.google.com/p/the-bus-pirate/).
 *
 * Initial written by Chris van Dongen, 2010.
 *
 * To the extent possible under law, the project has
 * waived all copyright and related or neighboring rights to Bus Pirate.
 *
 * For details see: http://creativecommons.org/publicdomain/zero/1.0/.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "pic.h"

#ifdef BP_ENABLE_PIC_SUPPORT

#include "base.h"

#include "bitbang.h"
#include "core.h"
#include "proc_menu.h"		// for the userinteraction subs
#include "aux_pin.h"

extern bus_pirate_configuration_t bus_pirate_configuration;
extern mode_configuration_t mode_configuration;
extern bool command_error;

static int picmode;
static int piccmddelay;

static void clock_out_zero(void);
static void clock_out_data(const uint16_t data, const uint8_t bits);
static uint16_t clock_in_data(const uint8_t bits);

void picinit(void)
{	int mode, delay;
	int interactive;

	consumewhitechars();
	mode=getint();
	consumewhitechars();
	delay=getint();
	interactive=0;

	if(!((mode>0)&&(mode<=2)))
	{	interactive=1;
	}

	if((delay>0)&&(delay<=2))
	{	piccmddelay=delay;
	}
	else
	{	interactive=1;
	}

	if(interactive)
	{	command_error=false;

		//bpWline("Commandmode");
		//bpWline("1. 6b/14b");
		//bpWline("2. 4b/16b");
		BPMSG1072;
	
		mode=getnumber(1,1,2,0); 

		//bpWline("Delay");
		BPMSG1073;
		delay=getnumber(1,1,2,0);
	}

	switch(mode)
	{	case 1:	picmode=PICMODE6;
				break;
		case 2: picmode=PICMODE4;
				break;
		default: break;
	}
	piccmddelay=delay;

	if(!interactive)
	{	//bpWstring("PIC(mod dly)=(");
		BPMSG1074;
		bp_write_dec_byte(picmode); bpSP;
		bp_write_dec_byte(piccmddelay);
		bp_write_line(")");
	}

	mode_configuration.high_impedance=1;				// to allow different Vcc 
	mode_configuration.int16=1;
	bitbang_set_pins_low(MOSI|CLK, PICSPEED);		// pull both pins to 0 before applying Vcc and Vpp
}

//Doesn't do much as the protocol defines that the pins need to be connected bedro power is applied.
void picinit_exc(void){mode_configuration.int16=1;}

void piccleanup(void)
{	mode_configuration.int16=0;				// other things are cleared except this one :D (we introduced it :D)
}

void picstart(void)					// switch  to commandmode
{	picmode|=PICCMD;
	//bpWstring("CMD");
	BPMSG1075;
	user_serial_transmit_character(0x30+(picmode&PICMODEMSK));			// display #commandbits 
	mode_configuration.int16=0;
	bpBR;
}

void picstop(void)					// switch to datamode
{	picmode&=PICMODEMSK;
	mode_configuration.int16=1;				// data is 14-16 bit
	//bpWline("DTA");
	BPMSG1076;
}

uint16_t clock_in_data(const uint8_t bits) {
    uint8_t index;
    uint16_t data;

    data = 0;
    for (index = 0; index < bits; index++) {
        data >>= 1;
        bitbang_set_pins_high(CLK, PICSPEED / 2);
        data |= (bitbang_read_pin(MOSI) == HIGH ? 1 : 0) << (1 << (bits - 1));
    }

	bitbang_set_pins_high(CLK, PICSPEED / 2);
	bitbang_set_pins_low(CLK, PICSPEED / 2);
	bitbang_set_pins_low(MOSI, PICSPEED / 5);

    return data;
}

unsigned int picread(void)
{
	unsigned int c;

	if(picmode&PICCMDMSK)
	{	//bpWline("no read");
		BPMSG1077;
		return 0;
	}

	c=0;

	switch(picmode&PICMODEMSK)		// make it future proof
	{	case PICMODE6:	bitbang_read_pin(MOSI);		// switch in to input
						bitbang_set_pins_high(CLK, PICSPEED/2);
						bitbang_set_pins_low(CLK, PICSPEED/2);
                        c = clock_in_data(14);
						break;
		case PICMODE4:	bitbang_read_pin(MOSI);
                        c = clock_in_data(16);
						break;
		default:		MSG_PIC_UNKNOWN_MODE;
					BPMSG1078;
						return 0;
	}

	return c;
}


void clock_out_data(const uint16_t data, const uint8_t bits) {
    uint8_t index;
    uint8_t mask;

    mask = 1;
    for (index = 0; index < bits; index++) {
        bitbang_set_pins_high(CLK, PICSPEED / 4);
        bitbang_set_pins((data & mask) == mask, MOSI, PICSPEED / 4);
        bitbang_set_pins_low(CLK, PICSPEED / 4);
		bitbang_set_pins_low(MOSI, PICSPEED / 4);
        mask <<= 1;
    }
}

void clock_out_zero(void) {
    bitbang_set_pins_high(CLK, PICSPEED / 4);
    bitbang_set_pins_low(MOSI, PICSPEED / 4);
    bitbang_set_pins_low(CLK, PICSPEED / 4);
    bitbang_set_pins_low(CLK, PICSPEED / 4);
}

unsigned int picwrite(unsigned int c)
{
	int mask;

	mask=0x01;

	if(picmode&PICCMDMSK)				// we got a command
	{	switch(picmode&PICMODEMSK)		// make it future proof
		{	case PICMODE6: clock_out_data(c, 6);
							break;
			case PICMODE4:	clock_out_data(c, 4);
							break;
			default:		//bpWline("unknown");
						BPMSG1078;
							return 0;
		}
		bp_delay_ms(piccmddelay);
	}
	else									// send data
	{	switch(picmode&PICMODEMSK)		// make it future proof
		{	case PICMODE6:
                            clock_out_zero();
                            clock_out_data(c, 14);
                            clock_out_zero();
							break;
			case PICMODE4:	clock_out_data(c, 16);
							break;
			default:		//bpWline("unknown");
						BPMSG1078;
							return 0;
		}
	}
	return 0x100; 	// no data to display 
}

void picmacro(unsigned int macro)
{	unsigned int temp;
	int i;

	switch(macro)
	{	case 0:	//bpWline("(1) get devID");
				BPMSG1079;
				break;
		case 1: switch(picmode&PICMODEMSK)
				{	case PICMODE6:	bus_pirate_configuration.quiet=1;				// turn echoing off
									picstart();
									picwrite(0);
									picstop();
									picwrite(0);				// advance to 0x2006 (devid)
									picstart();
									for(i=0; i<6; i++)
									{	picwrite(6);
									}
									picwrite(4);
									picstop();
									temp=picread();
									bus_pirate_configuration.quiet=0;				// turn echoing on
									//bpWstring("DevID = ");
									BPMSG1080;
									bp_write_hex_word(temp>>5);
									//bpWstring(" Rev = ");
									BPMSG1081;
									bp_write_hex_byte(temp&0x1f);
									bpBR;
									break;
					case PICMODE4:	
					default:		//bpWline("Not implemented (yet)");
									BPMSG1082;
				}
				//bpWline("Please exit PIC programming mode");
				BPMSG1083;
				break;
		default:
            MSG_UNKNOWN_MACRO_ERROR;
            break;
	}
}

void picpins(void) {
        #if defined(BUSPIRATEV4)
        BPMSG1262; //bpWline("-\t-\tPGC\tPGD");
        #else
       	BPMSG1232; //bpWline("PGC\tPGD\t-\t-");
        #endif
}

/*
0000 0000	return to main
0000 0001	id=PIC1
0000 0010	6b cmd
0000 0011	4b cmd
0000 01xx	xx ms delay

0000 1xxx	unimplemented

0001 0xyz	PWM|VREG|PULLUP
0001 1xyz   AUX|MISO|CS

01xx xxxx	just send cmd xxxxxxx
10xx xxxx	send cmd xxxxxxx and next two bytes (14/16 bits)
11xx xxxx	send cmd xxxxxxx and read two bytes

*/

void binpic(void)
{	unsigned char cmd;
	int ok;
	unsigned int temp;

	MSG_PIC_MODE_IDENTIFIER;
	mode_configuration.high_impedance=1;				// to allow different Vcc 
	bitbang_set_pins_low(MOSI|CLK, PICSPEED);		// pull both pins to 0 before applying Vcc and Vpp
	picmode=PICMODE6;
	piccmddelay=2;

	while(1)
	{	cmd=user_serial_read_byte();

		switch(cmd&0xC0)
		{	case 0x00:	ok=1;
						switch(cmd&0xF0)
						{	case 0x00:	switch(cmd)
										{	case 0x00:	return;
											case 0x01:	MSG_PIC_MODE_IDENTIFIER;
														break;
											case 0x02:	picmode=PICMODE6;
														break;
											case 0x03:	picmode=PICMODE4;
														break;
											case 0x04:
											case 0x05:
											case 0x06:
											case 0x07:	piccmddelay=(cmd-0x04);
														break;
											default:	ok=0;
										}
										break;
							case 0x10:	if(cmd&0x08)
										{	if(cmd&0x04)
											{	bitbang_set_pins_high(AUX0 ,5);
											}
											else
											{	bitbang_set_pins_low(AUX0 ,5);
											}
											if(cmd&0x02)
											{	bitbang_set_pins_high(MISO ,5);
											}
											else
											{	bitbang_set_pins_low(MISO ,5);
											}
											if(cmd&0x01)
											{	bitbang_set_pins_high(CS ,5);
											}
											else
											{	bitbang_set_pins_low(CS ,5);
											}
										}
										else
										{	if(cmd&0x04)	// pwm?
											{
                                            bp_update_pwm(100, 50);
											}
											else
											{
                                            bp_update_pwm(PWM_OFF, PWM_OFF);
											}
											if(cmd&0x02)	// vreg on
											{	BP_VREG_ON();
												//modeConfig.vregEN=1;
											}
											else
											{	BP_VREG_OFF();
												//modeConfig.vregEN=0;
											}
											if(cmd&0x01)	// pullup on
											{	BP_PULLUP_ON(); 
//												modeConfig.pullupEN=1;
											}
											else
											{	BP_PULLUP_OFF();
//												modeConfig.pullupEN=0;
											}
										}
										break;
							default:	ok=0;
						}
						if(ok)
						{	user_serial_transmit_character(1);
						}
						else
						{	user_serial_transmit_character(0);
						}
						break;
			case 0x40:	picmode|=PICCMD;
						picwrite(cmd&0x3F);
						picmode&=PICMODEMSK;
						user_serial_transmit_character(1);
						break;
			case 0x80:	picmode|=PICCMD;
						picwrite(cmd&0x3F);
						picmode&=PICMODEMSK;
						temp=user_serial_read_byte();
						temp<<=8;
						temp|=user_serial_read_byte();
						picwrite(temp);
						user_serial_transmit_character(1);
						break;
			case 0xC0:	picmode|=PICCMD;
						picwrite(cmd&0x3F);
						picmode&=PICMODEMSK;
						user_serial_transmit_character(1);
						temp=picread();
						user_serial_transmit_character(temp>>8);
						user_serial_transmit_character(temp&0x0FF);
						break;
		}
	}
}

#endif /* BP_ENABLE_PIC_SUPPORT */