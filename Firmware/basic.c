
/*
 * This file is part of the Bus Pirate project
 * (http://code.google.com/p/the-bus-pirate/).
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

#include "basic.h"

#ifdef BP_ENABLE_BASIC_SUPPORT

#if defined(BUSPIRATEV3) && defined(BP_BASIC_I2C_FILESYSTEM)
#warning "BP_BASIC_I2C_FILESYSTEM is not supported on v3 boards!"
#undef BP_BASIC_I2C_FILESYSTEM
#endif /* BP_BASIC_I2C_FILESYSTEM */

#if BP_BASIC_PROGRAM_SPACE <= 0
#error "Invalid BASIC program space value"
#endif /* BP_BASIC_PROGRAM_SPACE <= 0 */

#if BP_BASIC_NESTED_FOR_LOOP_COUNT <= 1
#error "Invalid nested BASIC FOR-LOOP count"
#endif /* BP_BASIC_NESTED_FOR_LOOP_COUNT <= 1 */

#if BP_BASIC_STACK_FRAMES_DEPTH <= 1
#error "Invalid BASIC stack depth"
#endif /* BP_BASIC_STACK_FRAMES_DEPTH <= 1*/

/**
 * How many variables the BASIC interpreter can handle.
 *
 * Currently set to 26 to handle variables identified from 'A' to 'Z'.
 */
#define BP_BASIC_VARIABLES_COUNT 26

#include "aux_pin.h"
#include "base.h"
#include "bitbang.h"
#include "core.h"
#include "proc_menu.h"

#define TOKENS 0x80
#define TOK_LET 0x80
#define TOK_IF 0x81
#define TOK_THEN 0x82
#define TOK_ELSE 0x83
#define TOK_GOTO 0x84
#define TOK_GOSUB 0x85
#define TOK_RETURN 0x86
#define TOK_REM 0x87
#define TOK_PRINT 0x88
#define TOK_INPUT 0x89
#define TOK_FOR 0x8A
#define TOK_TO 0x8B
#define TOK_NEXT 0x8C
#define TOK_READ 0x8D
#define TOK_DATA 0x8E
#define TOK_STARTR 0x8F
#define TOK_START 0x90
#define TOK_STOPR 0x91
#define TOK_STOP 0x92
#define TOK_SEND 0x93
#define TOK_RECEIVE 0x94
#define TOK_CLK 0x95
#define TOK_DAT 0x96
#define TOK_BITREAD 0x97
#define TOK_ADC 0x98
#define TOK_AUXPIN 0x99
#define TOK_PSU 0x9A
#define TOK_PULLUP 0x9B
#define TOK_DELAY 0x9C

#define TOK_AUX 0x9D
#define TOK_FREQ 0x9E
#define TOK_DUTY 0x9F

#define TOK_MACRO 0xA0
#define TOK_END 0xA1

#define TOK_LEN 0xE0

#define NUMTOKEN (TOK_END - TOKENS) + 1

#define STAT_LET "LET"
#define STAT_IF "IF"
#define STAT_THEN "THEN"
#define STAT_ELSE "ELSE"
#define STAT_GOTO "GOTO"
#define STAT_GOSUB "GOSUB"
#define STAT_RETURN "RETURN"
#define STAT_REM "REM"
#define STAT_PRINT "PRINT"
#define STAT_INPUT "INPUT"
#define STAT_FOR "FOR"
#define STAT_TO "TO"
#define STAT_NEXT "NEXT"
#define STAT_END "END"
#define STAT_READ "READ"
#define STAT_DATA "DATA"
#define STAT_START "START"
#define STAT_STARTR "STARTR"
#define STAT_STOP "STOP"
#define STAT_STOPR "STOPR"
#define STAT_SEND "SEND"
#define STAT_RECEIVE "RECEIVE"
#define STAT_CLK "CLK"
#define STAT_DAT "DAT"
#define STAT_BITREAD "BITREAD"
#define STAT_ADC "ADC"
#define STAT_AUX "AUX"
#define STAT_PSU "PSU"
#define STAT_PULLUP "PULLUP"
#define STAT_DELAY "DELAY"

#define STAT_AUXPIN "AUXPIN"
#define STAT_FREQ "FREQ"
#define STAT_DUTY "DUTY"
#define STAT_MACRO "MACRO"

#define NOERROR 1
#define NOLEN 2
#define SYNTAXERROR 3
#define FORERROR 4
#define NEXTERROR 5
#define GOTOERROR 6
#define STACKERROR 7
#define RETURNERROR 8
#define DATAERROR 9

extern bus_pirate_configuration_t bus_pirate_configuration;
extern mode_configuration_t mode_configuration;
extern command_t last_command;
extern bus_pirate_protocol_t enabled_protocols[ENABLED_PROTOCOLS_COUNT];

typedef struct {
  unsigned int from;
  unsigned int var;
  unsigned int to;
} __attribute__((packed)) basic_for_loop_t;

static int basic_variables[BP_BASIC_VARIABLES_COUNT];
static int basic_stack[BP_BASIC_STACK_FRAMES_DEPTH];
static basic_for_loop_t basic_nested_for_loops[BP_BASIC_NESTED_FOR_LOOP_COUNT];
static unsigned int basic_program_counter;
static unsigned int basic_current_nested_for_index;
static unsigned int basic_current_stack_frame;
static unsigned int basic_data_read_pointer;

static char *tokens[NUMTOKEN + 1] = {
    STAT_LET,     // 0x80
    STAT_IF,      // 0x81
    STAT_THEN,    // 0x82
    STAT_ELSE,    // 0x83
    STAT_GOTO,    // 0x84
    STAT_GOSUB,   // 0x85
    STAT_RETURN,  // 0x86
    STAT_REM,     // 0x87
    STAT_PRINT,   // 0x88
    STAT_INPUT,   // 0x89
    STAT_FOR,     // 0x8A
    STAT_TO,      // 0x8b
    STAT_NEXT,    // 0x8c
    STAT_READ,    // 0x8d
    STAT_DATA,    // 0x8e
    STAT_STARTR,  // 0x8f
    STAT_START,   // 0x90
    STAT_STOPR,   // 0x91
    STAT_STOP,    // 0x92
    STAT_SEND,    // 0x93
    STAT_RECEIVE, // 0x94
    STAT_CLK,     // 0x95
    STAT_DAT,     // 0x96
    STAT_BITREAD, // 0x97
    STAT_ADC,     // 0x98
    STAT_AUXPIN,  // 0x99
    STAT_PSU,     // 0x9a
    STAT_PULLUP,  // 0x9b
    STAT_DELAY,   // 0x9c
    STAT_AUX,     // 0x9d
    STAT_FREQ,    // 0x9e
    STAT_DUTY,    // 0x9f

    STAT_MACRO, // 0xA0
    STAT_END,   // 0xa1
};

static uint8_t basic_program_area[BP_BASIC_PROGRAM_SPACE]; /*={

// basic basic test :D
#ifdef BASICTEST
TOK_LEN+10, 0, 100, TOK_REM, 'b', 'a', 's', 'i', 'c', 't', 'e', 's', 't',
TOK_LEN+ 7, 0, 110, TOK_LET, 'A', '=', 'C', '+', '1', '6',
TOK_LEN+ 6, 0, 120, TOK_FOR, 'B', '=', '1', TOK_TO, '3',
TOK_LEN+ 6, 0, 125, TOK_FOR, 'D', '=', '0', TOK_TO, '1',
TOK_LEN+23, 0, 130, TOK_PRINT, '\"', 'A', '=', '\"', ';', 'A', ';', '\"', ' ',
'B', '=', '\"', ';', 'B', ';', '\"', ' ', 'D', '=', '\"', ';', 'D', //';',
TOK_LEN+ 2, 0, 135, TOK_NEXT, 'D',
TOK_LEN+ 2, 0, 140, TOK_NEXT, 'B',
TOK_LEN+12, 0, 200, TOK_INPUT, '\"', 'E', 'n', 't', 'e', 'r', ' ', 'C', '\"',
',','C',
TOK_LEN+ 2, 0, 201, TOK_READ, 'C',
TOK_LEN+ 5, 0, 202, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+ 2, 0, 203, TOK_READ, 'C',
TOK_LEN+ 5, 0, 204, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+ 2, 0, 205, TOK_READ, 'C',
TOK_LEN+ 5, 0, 206, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+ 2, 0, 207, TOK_READ, 'C',
TOK_LEN+ 5, 0, 210, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+26, 0, 220, TOK_IF, 'C', '=', '2', '0', TOK_THEN, TOK_PRINT, '\"', 'C',
'=', '2', '0', '!', '!', '\"', ';', TOK_ELSE, TOK_PRINT, '\"', 'C', '!', '=',
'2', '0', '"', ';',
TOK_LEN+ 1, 0, 230, TOK_END,
TOK_LEN+ 7, 3, 232, TOK_PRINT, '\"', 'C', '=', '\"', ';', 'C',
TOK_LEN+ 1, 3, 242, TOK_RETURN,
TOK_LEN+ 6, 7, 208, TOK_DATA, '1', ',', '2', ',', '3',
TOK_LEN+ 3, 7, 218, TOK_DATA, '2', '0',
TOK_LEN+ 1, 255, 255, TOK_END,
#endif


// I2C basic test (24lc02)

#ifdef BASICTEST_I2C
TOK_LEN+18, 0, 100, TOK_REM, 'I', '2', 'C', ' ', 't', 'e', 's', 't', ' ', '(',
'2', '4', 'l', 'c', '0', '2', ')',
TOK_LEN+ 2, 0, 110, TOK_PULLUP, '1',
TOK_LEN+ 2, 0, 120, TOK_PSU, '1',
TOK_LEN+ 4, 0, 130, TOK_DELAY, '2', '5', '5',
TOK_LEN+ 1, 0, 140, TOK_STOP,
TOK_LEN+ 5, 0, 150, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+ 1, 0, 200, TOK_START,
TOK_LEN+ 4, 0, 210, TOK_SEND, '1', '6', '0',
TOK_LEN+ 2, 0, 220, TOK_SEND, '0',
TOK_LEN+ 6, 0, 230, TOK_FOR, 'A', '=', '1', TOK_TO, '8',
TOK_LEN+ 2, 0, 240, TOK_READ, 'B',
TOK_LEN+ 2, 0, 250, TOK_SEND, 'B',
TOK_LEN+ 2, 0, 200, TOK_NEXT, 'A',
TOK_LEN+ 1, 1,   4, TOK_STOP,
TOK_LEN+ 4, 1,  14, TOK_DELAY, '2', '5', '5',
TOK_LEN+ 5, 1,  24, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+ 2, 1,  34, TOK_PSU, '0',
TOK_LEN+ 2, 1,  44, TOK_PULLUP, '0',
TOK_LEN+ 1, 1,  54, TOK_END,
TOK_LEN+13, 3, 232, TOK_REM, 'D', 'u', 'm', 'p', ' ', '8', ' ', 'b', 'y', 't',
'e', 's',
TOK_LEN+ 1, 3, 242, TOK_START,
TOK_LEN+ 4, 3, 252, TOK_SEND, '1', '6', '0',
TOK_LEN+ 2, 4,   6, TOK_SEND, '0',
TOK_LEN+ 1, 4,  16, TOK_START,
TOK_LEN+ 4, 4,  26, TOK_SEND, '1', '6', '1',
TOK_LEN+ 7, 4,  36, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 7, 4,  46, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 7, 4,  56, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 7, 4,  66, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 7, 4,  76, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 7, 4,  86, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 7, 4,  96, TOK_PRINT, TOK_RECEIVE, ';', '"', ' ', '"', ';',
TOK_LEN+ 2, 4, 106, TOK_PRINT, TOK_RECEIVE,
TOK_LEN+ 1, 4, 116, TOK_STOP,
TOK_LEN+ 1, 4, 116, TOK_RETURN,
TOK_LEN+16, 7, 208, TOK_DATA, '2', '5' ,'5', ',', '2', '5' ,'5', ',','2', '5'
,'5', ',','2', '5' ,'5',
TOK_LEN+16, 7, 218, TOK_DATA, '2', '5' ,'5', ',', '2', '5' ,'5', ',','2', '5'
,'5', ',','2', '5' ,'5',
#endif

// UART test (serial rfid reader from seed)
#ifdef BASICTEST_UART
TOK_LEN+15, 0, 100, TOK_REM, 'U', 'A', 'R', 'T', ' ', 't', 'e', 's', 't', ' ',
'r', 'f', 'i', 'd',
TOK_LEN+ 2, 0, 110, TOK_PSU, '1',
TOK_LEN+ 4, 0, 120, TOK_DELAY, '2', '5', '5',
TOK_LEN+ 5, 0, 130, TOK_GOSUB, '1', '0', '0', '0',
TOK_LEN+ 3, 0, 135, TOK_DELAY, '1', '0',
//TOK_LEN+12, 0, 140, TOK_IF, TOK_RECEIVE, '!', '=', '5', '2', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 145, TOK_DELAY, '1',
//TOK_LEN+12, 0, 150, TOK_IF, TOK_RECEIVE, '!', '=', '5', '4', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 155, TOK_DELAY, '1',
//TOK_LEN+12, 0, 160, TOK_IF, TOK_RECEIVE, '!', '=', '4', '8', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 165, TOK_DELAY, '1',
//TOK_LEN+12, 0, 170, TOK_IF, TOK_RECEIVE, '!', '=', '4', '8', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 175, TOK_DELAY, '1',
//TOK_LEN+12, 0, 180, TOK_IF, TOK_RECEIVE, '!', '=', '5', '4', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 185, TOK_DELAY, '1',
//TOK_LEN+12, 0, 190, TOK_IF, TOK_RECEIVE, '!', '=', '5', '3', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 195, TOK_DELAY, '1',
//TOK_LEN+12, 0, 200, TOK_IF, TOK_RECEIVE, '!', '=', '5', '5', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 205, TOK_DELAY, '1',
//TOK_LEN+12, 0, 210, TOK_IF, TOK_RECEIVE, '!', '=', '5', '5', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 215, TOK_DELAY, '1',
//TOK_LEN+12, 0, 220, TOK_IF, TOK_RECEIVE, '!', '=', '5', '7', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 225, TOK_DELAY, '1',
//TOK_LEN+12, 0, 230, TOK_IF, TOK_RECEIVE, '!', '=', '5', '4', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 235, TOK_DELAY, '1',
//TOK_LEN+12, 0, 240, TOK_IF, TOK_RECEIVE, '!', '=', '6', '7', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
//TOK_LEN+ 2, 0, 245, TOK_DELAY, '1',
//TOK_LEN+12, 0, 250, TOK_IF, TOK_RECEIVE, '!', '=', '5', '0', TOK_THEN,
TOK_GOTO, '2', '0', '2', '0',
TOK_LEN+ 6, 0, 140, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 145, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 150, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 155, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 160, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 165, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 170, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 175, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 180, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 185, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 190, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 195, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 200, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 205, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 210, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 215, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 220, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 225, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 230, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 235, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 240, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 3, 0, 245, TOK_DELAY, '5', '0',
TOK_LEN+ 6, 0, 250, TOK_PRINT, TOK_RECEIVE, '"', ' ', '"', ';',
//TOK_LEN+ 5, 1,   4, TOK_GOTO, '2', '0', '0', '0',
TOK_LEN+ 4, 1,  14, TOK_GOTO, '1', '3', '0',
TOK_LEN+13, 3, 232, TOK_REM, 'W', 'a', 'i', 't', ' ', 'f', 'o', 'r', ' ', 'S',
'T', 'X',
TOK_LEN+ 4, 3, 242, TOK_LET, 'A', '=', TOK_RECEIVE,
TOK_LEN+12, 3, 252, TOK_IF, 'A', '=', '2', TOK_THEN, TOK_RETURN, TOK_ELSE,
TOK_GOTO, '1', '0', '1', '0',
//TOK_LEN+ 8, 7, 208, TOK_PRINT, '"', 'V', 'A', 'L', 'I', 'D', '"',
//TOK_LEN+ 4, 7, 218, TOK_GOTO, '1', '3', '0',
//TOK_LEN+10, 7, 228, TOK_PRINT, '"', 'I', 'N', 'V', 'A', 'L', 'I', 'D', '"',
//TOK_LEN+ 4, 7, 238, TOK_GOTO, '1', '3', '0',
#endif

// raw3wire test (atiny85)
#ifdef BASICTEST_R3W
TOK_LEN+22, 0, 10, TOK_REM, 'r', '2', 'w', 'i', 'r', 'e', ' ', 't', 'e', 's',
't', ' ', '(', 'a', 't', 'i', 'n', 'y', '8', '5', ')',
TOK_LEN+ 2, 0, 100, TOK_PULLUP, '1',
TOK_LEN+ 2, 0, 110, TOK_CLK, '0',
TOK_LEN+ 2, 0, 120, TOK_DAT, '0',
TOK_LEN+ 2, 0, 130, TOK_AUX, '0',
TOK_LEN+ 2, 0, 140, TOK_PSU, '1',
TOK_LEN+ 4, 0, 150, TOK_DELAY, '2', '5', '5',
TOK_LEN+ 1, 0, 160, TOK_STARTR,
TOK_LEN+ 7, 0, 170, TOK_LET, 'A', '=', TOK_SEND, '1', '7', '2',
TOK_LEN+ 6, 0, 180, TOK_LET, 'B', '=', TOK_SEND, '8', '3',
TOK_LEN+ 5, 0, 190, TOK_LET, 'C', '=', TOK_SEND, '0',
TOK_LEN+ 5, 0, 200, TOK_LET, 'D', '=', TOK_SEND, '0',
TOK_LEN+ 8, 0, 210, TOK_IF, 'C', '!', '=', '8', '3', TOK_THEN, TOK_END,
TOK_LEN+15, 0, 220, TOK_PRINT, '"', 'F', 'O', 'U', 'N', 'D', ' ', 'D', 'E', 'V',
'I', 'C', 'E', '"',
TOK_LEN+13, 0, 230, TOK_PRINT, '"', 'd', 'e', 'v', 'i', 'c', 'e', 'I', 'D', ':',
'"', ';',
TOK_LEN+ 6, 0, 240, TOK_LET, 'A', '=', TOK_SEND, '4', '8',
TOK_LEN+ 5, 0, 250, TOK_LET, 'B', '=', TOK_SEND, '0',
TOK_LEN+ 5, 1,   4, TOK_LET, 'C', '=', TOK_SEND, '0',
TOK_LEN+ 5, 1,  14, TOK_LET, 'D', '=', TOK_SEND, '0',
TOK_LEN+ 7, 1,  24, TOK_PRINT, 'D', ';', '"', ' ', '"', ';',
TOK_LEN+ 6, 1,  34, TOK_LET, 'A', '=', TOK_SEND, '4', '8',
TOK_LEN+ 5, 1,  44, TOK_LET, 'B', '=', TOK_SEND, '0',
TOK_LEN+ 5, 1,  54, TOK_LET, 'C', '=', TOK_SEND, '1',
TOK_LEN+ 5, 1,  64, TOK_LET, 'D', '=', TOK_SEND, '0',
TOK_LEN+ 7, 1,  74, TOK_PRINT, 'D', ';', '"', ' ', '"', ';',
TOK_LEN+ 6, 1,  84, TOK_LET, 'A', '=', TOK_SEND, '4', '8',
TOK_LEN+ 5, 1,  94, TOK_LET, 'B', '=', TOK_SEND, '0',
TOK_LEN+ 5, 1, 104, TOK_LET, 'C', '=', TOK_SEND, '2',
TOK_LEN+ 5, 1, 114, TOK_LET, 'D', '=', TOK_SEND, '0',
TOK_LEN+ 2, 1, 124, TOK_PRINT, 'D',
TOK_LEN+14, 1, 134, TOK_PRINT, '"', 'd', 'e', 'v', 'i', 'c', 'e', ' ', 'i', 's',
' ', '"', ';',
TOK_LEN+ 6, 1,  84, TOK_LET, 'A', '=', TOK_SEND, '8', '8',
TOK_LEN+ 5, 1,  94, TOK_LET, 'B', '=', TOK_SEND, '0',
TOK_LEN+ 5, 1, 104, TOK_LET, 'C', '=', TOK_SEND, '2',
TOK_LEN+ 5, 1, 114, TOK_LET, 'D', '=', TOK_SEND, '0',
TOK_LEN+26, 1, 124, TOK_IF, 'D', '=', '3', TOK_THEN, TOK_PRINT, '"', 'U', 'N',
'L', 'O', 'C', 'K', 'E', 'D', '"', TOK_ELSE, TOK_PRINT, '"', 'L', 'O', 'C', 'K',
'E', 'D','"',
TOK_LEN+ 1, 1, 134, TOK_END,
#endif

#ifdef BASICTEST_PIC10

TOK_LEN+15, 0, 100, TOK_REM, 'P', 'R', 'O', 'G', 'R', 'A', 'M', ' ', 'P', 'I',
'C', '1', '0', 'F',
TOK_LEN+ 7, 0, 110, TOK_FOR, 'A', '=', '1', TOK_TO, '2', '4',
TOK_LEN+ 1, 0, 120, TOK_START,
TOK_LEN+ 2, 0, 130, TOK_SEND, '2',
TOK_LEN+ 1, 0, 140, TOK_STOP,
TOK_LEN+ 2, 0, 150, TOK_READ, 'B',
TOK_LEN+ 2, 0, 160, TOK_SEND, 'B',
TOK_LEN+ 1, 0, 170, TOK_START,
TOK_LEN+ 2, 0, 180, TOK_SEND, '8',
TOK_LEN+ 2, 0, 190, TOK_DELAY, '2',
TOK_LEN+ 3, 0, 200, TOK_SEND, '1', '4',
TOK_LEN+ 2, 0, 210, TOK_SEND, '6',
TOK_LEN+ 1, 0, 210, TOK_STOP,
TOK_LEN+ 2, 0, 220, TOK_NEXT, 'A',
TOK_LEN+ 1, 0, 230, TOK_START,
TOK_LEN+ 9, 0, 240, TOK_FOR, 'A', '=', '2', '5', TOK_TO, '5', '1', '2',
TOK_LEN+ 2, 0, 250, TOK_SEND, '6',
TOK_LEN+ 2, 1,   4, TOK_NEXT, 'A',
TOK_LEN+ 2, 1,  14, TOK_SEND, '2',
TOK_LEN+ 1, 1,  24, TOK_STOP,
TOK_LEN+ 2, 1,  34, TOK_READ, 'B',
TOK_LEN+ 2, 1,  44, TOK_SEND, 'B',
TOK_LEN+ 1, 1,  54, TOK_START,
TOK_LEN+ 2, 1,  64, TOK_SEND, '8',
TOK_LEN+ 2, 1,  74, TOK_DELAY, '2',
TOK_LEN+ 3, 1,  84, TOK_SEND, '1', '4',
TOK_LEN+ 1, 1,  94, TOK_STOP,
TOK_LEN+ 1, 1, 104, TOK_END,
//TOK_LEN+11, 3, 232, TOK_REM, 'C', 'O', 'N', 'F', 'I', 'G', 'W', 'O', 'R', 'D',
TOK_LEN+ 5, 3, 232, TOK_DATA, '4', '0', '7', '5',
//TOK_LEN+ 5, 7, 208, TOK_REM, 'M', 'A', 'I', 'N',
TOK_LEN+26, 7, 208, TOK_DATA, '3', '7', ',', '1', '0', '2', '9', ',', '2', '5',
'7', '3', ',', '3', '3', '2', '2', ',', '4', '9', ',', '3', '3', '2', '7',
TOK_LEN+26, 7, 218, TOK_DATA, '5', '0', ',', '7', '5', '3', ',', '2', '5', '7',
'0', ',', '2', '0', '4', '8', ',', '7', '5', '4', ',', '2', '5', '7', '0',
TOK_LEN+25, 7, 228, TOK_DATA, '2', '5', '6', '7', ',', '3', '2', '6', '4', ',',
'2', ',', '3', '0', '7', '2', ',', '3', '8', ',', '3', '3', '2', '3',
TOK_LEN+21, 7, 238, TOK_DATA, '6', ',', '3', '0', '7', '6', ',', '4', '2', '2',
',', '2', '3', '0', '7', ',', '2', '5', '7', '9',
//TOK_LEN+ 7,11, 184, TOK_REM, 'O', 'S', 'C', 'C', 'A', 'L',
TOK_LEN+ 5,11, 184, TOK_DATA, '3', '0', '9', '6',

// data statements and rems aren't mixing well!
#endif
#ifdef BASICTEST_PIC10_2

TOK_LEN+12, 0, 100, TOK_REM, 'D', 'U', 'M', 'P', ' ', 'P', 'I', 'C', '1', '0',
'F',
TOK_LEN+ 8, 0, 110, TOK_FOR, 'A', '=', '1', TOK_TO, '5', '1', '8',
TOK_LEN+ 1, 0, 120, TOK_START,
TOK_LEN+ 2, 0, 130, TOK_SEND, '4',
TOK_LEN+ 1, 0, 140, TOK_STOP,
TOK_LEN+ 7, 0, 150, TOK_PRINT, TOK_RECEIVE, ';', '"', ',', '"', ';',
TOK_LEN+ 1, 0, 160, TOK_START,
TOK_LEN+ 2, 0, 170, TOK_SEND, '6',
TOK_LEN+ 2, 0, 180, TOK_NEXT, 'A',
TOK_LEN+ 1, 0, 190, TOK_END,
#endif


0x00,0x00,
};
*/

/**
 * Scans the currently loaded program to find the token index of the given line
 * number.
 *
 * @param[in] line the line number to obtain an offset for.
 * @return the offset inside basic_program_area where the given line starts, or
 * -1 if no such line was found.
 */
static int search_line_number(uint16_t line);

static int handle_special_token(const uint8_t token);

static void list(void);
static void interpreter(void);
static void handle_else_statement(void);
static int get_number_or_variable(void);
static int get_multiplication_division_bitwise_ops(void);
static int assign(void);
static void interpreter(void);
static void list(void);

#ifdef BP_BASIC_I2C_FILESYSTEM
static void directory(void);
static void save(void);
static void format(void);
static void load(void);
static void waiteeprom(void);
#endif /* BP_BASIC_I2C_FILESYSTEM */

void handle_else_statement(void) {
  if (basic_program_area[basic_program_counter] == TOK_ELSE) {
    basic_program_counter++;
    while (basic_program_area[basic_program_counter] <= TOK_LEN) {
      basic_program_counter++;
    }
  }
}

int search_line_number(uint16_t line) {
  size_t index;
  uint8_t token_length;
  uint16_t current_line_number;

  index = 0;
  for (;;) {
    if (basic_program_area[index] <= TOK_LEN) {
      return -1;
    }
    token_length = basic_program_area[index] - TOK_LEN;
    current_line_number =
        (basic_program_area[index + 1] << 8) + basic_program_area[index + 2];
    if (line == current_line_number) {
      return index;
    }
    index += token_length + 3;
  }

  return -1;
}

int handle_special_token(const uint8_t token) {
  switch (token) {

  case TOK_RECEIVE:
    return enabled_protocols[bus_pirate_configuration.bus_mode].read();

  case TOK_SEND:
    return enabled_protocols[bus_pirate_configuration.bus_mode].send(assign());

  case TOK_AUX:
    return bp_aux_pin_read();

  case TOK_DAT:
    return enabled_protocols[bus_pirate_configuration.bus_mode].data_state();

  case TOK_BITREAD:
    return enabled_protocols[bus_pirate_configuration.bus_mode].read_bit();

  case TOK_PSU:
    return BP_VREGEN;

  case TOK_PULLUP:
    return ~BP_PULLUP;

  case TOK_ADC: {
    int adc_measurement;

    ADCON();
    adc_measurement = 0;//bp_read_adc(BP_ADC_PROBE);
    ADCOFF();

    return adc_measurement;
  }

  default:
    return 0;
  }
}

int get_number_or_variable(void) {
  int temp;

  temp = 0;

  if ((basic_program_area[basic_program_counter] == '(')) {
    basic_program_counter++;
    temp = assign();
    if ((basic_program_area[basic_program_counter] == ')')) {
      basic_program_counter++;
    }
  } else {
    if ((basic_program_area[basic_program_counter] >= 'A') &&
        (basic_program_area[basic_program_counter] <= 'Z')) {
      return basic_variables[basic_program_area[basic_program_counter++] - 'A'];
    }

    if (basic_program_area[basic_program_counter] > TOKENS) {
      return handle_special_token(basic_program_area[basic_program_counter++]);
    }

    while ((basic_program_area[basic_program_counter] >= '0') &&
           (basic_program_area[basic_program_counter] <= '9')) {
      temp *= 10;
      temp += basic_program_area[basic_program_counter] - '0';
      basic_program_counter++;
    }
  }

  return temp;
}

int get_multiplication_division_bitwise_ops(void) {
  int temp;

  temp = get_number_or_variable();

  for (;;) {
    switch (basic_program_area[basic_program_counter++]) {
    case '*':
      temp *= get_number_or_variable();
      break;

    case '/':
      temp /= get_number_or_variable();
      break;

    case '&':
      temp &= get_number_or_variable();
      break;

    case '|':
      temp |= get_number_or_variable();
      break;

    default:
      return temp;
    }
  }
}

int assign(void) {
  unsigned int temp;

  temp = get_multiplication_division_bitwise_ops();

  for (;;) {
    switch (basic_program_area[basic_program_counter++]) {
    case '-':
      temp -= get_multiplication_division_bitwise_ops();
      break;

    case '+':
      temp += get_multiplication_division_bitwise_ops();
      break;

    case '>':
      if (basic_program_area[basic_program_counter + 1] == '=') {
        temp = (temp >= get_multiplication_division_bitwise_ops());
        basic_program_counter++;
      } else {
        temp = (temp > get_multiplication_division_bitwise_ops());
      }
      break;

    case '<':
      if (basic_program_area[basic_program_counter + 1] == '>') {
        temp = (temp != get_multiplication_division_bitwise_ops());
        basic_program_counter++;
      } else if (basic_program_area[basic_program_counter + 1] == '=') {
        temp = (temp <= get_multiplication_division_bitwise_ops());
        basic_program_counter++;
      } else {
        temp = (temp < get_multiplication_division_bitwise_ops());
      }
      break;

    case '=':
      temp = (temp == get_multiplication_division_bitwise_ops());
      break;

    default:
      return temp;
    }
  }
}

void interpreter(void) {
  int len = 0;
  unsigned int lineno;
  int i;
  int stop;
  int pcupdated;
  int ifstat;

  int temp;

  basic_program_counter = 0;
  stop = 0;
  pcupdated = 0;
  ifstat = 0;
  basic_current_nested_for_index = 0;
  basic_current_stack_frame = 0;
  basic_data_read_pointer = 0;
  lineno = 0;
  bus_pirate_configuration.quiet = ON;

  memset((void *)&basic_variables, 0, sizeof(basic_variables));

  while (!stop) {
    if (!ifstat) {
      if (basic_program_area[basic_program_counter] < TOK_LEN) {
        stop = NOLEN;
        break;
      }

      len = basic_program_area[basic_program_counter] - TOK_LEN;
      lineno = ((basic_program_area[basic_program_counter + 1] << 8) |
                basic_program_area[basic_program_counter + 2]);
    }

    ifstat = 0;

    switch (basic_program_area[basic_program_counter + 3]) // first token
    {
    case TOK_LET:
      pcupdated = 1;
      basic_program_counter += 6;

      basic_variables[basic_program_area[basic_program_counter - 2] - 0x41] =
          assign();
      handle_else_statement();
      break;

    case TOK_IF:
      pcupdated = 1;
      basic_program_counter += 4;

      if (assign()) {
        if (basic_program_area[basic_program_counter++] == TOK_THEN) {
          ifstat = 1;
          basic_program_counter -= 3; // simplest way (for now)
        }
      } else {
        while ((basic_program_area[basic_program_counter] != TOK_ELSE) &&
               (basic_program_area[basic_program_counter] <= TOK_LEN)) {
          basic_program_counter++;
        }
        if (basic_program_area[basic_program_counter] == TOK_ELSE) {
          ifstat = 1;
          basic_program_counter -= 2; // simplest way (for now)
        }
      }
      break;

    case TOK_GOTO:
      pcupdated = 1;
      basic_program_counter += 4;

      temp = search_line_number(assign());
      if (temp != -1) {
        basic_program_counter = temp;
      } else {
        stop = GOTOERROR;
      }
      break;

    case TOK_GOSUB:
      pcupdated = 1;
      basic_program_counter += 4;

      if (basic_current_stack_frame < BP_BASIC_STACK_FRAMES_DEPTH) {
        basic_stack[basic_current_stack_frame] =
            basic_program_counter + len - 1;
        basic_current_stack_frame++;
        temp = search_line_number(assign());
        // bpSP; bpWinthex(temp); bpSP;
        if (temp != -1) {
          basic_program_counter = temp;
        } else {
          stop = GOTOERROR;
        }
      } else {
        stop = STACKERROR;
      }
      break;

    case TOK_RETURN:
      bp_write_string(STAT_RETURN);

      pcupdated = 1;
      if (basic_current_stack_frame) {
        basic_program_counter = basic_stack[--basic_current_stack_frame];
      } else {
        stop = RETURNERROR;
      }
      break;

    case TOK_REM:
      pcupdated = 1;
      basic_program_counter += len + 3;
      break;

    case TOK_PRINT:
      pcupdated = 1;
      basic_program_counter += 4;
      while ((basic_program_area[basic_program_counter] < TOK_LEN) &&
             (basic_program_area[basic_program_counter] != TOK_ELSE)) {
        if (basic_program_area[basic_program_counter] ==
            '\"') // is it a string?
        {
          basic_program_counter++;
          while (basic_program_area[basic_program_counter] != '\"') {
            bus_pirate_configuration.quiet = 0;
            user_serial_transmit_character(basic_program_area[basic_program_counter++]);
            bus_pirate_configuration.quiet = 1;
          }
          basic_program_counter++;
        } else if (((basic_program_area[basic_program_counter] >= 'A') &&
                    (basic_program_area[basic_program_counter] <= 'Z')) ||
                   ((basic_program_area[basic_program_counter] >= TOKENS) &&
                    (basic_program_area[basic_program_counter] < TOK_LEN))) {
          temp = assign();
          bus_pirate_configuration.quiet = 0;
          bp_write_dec_word(temp);
          bus_pirate_configuration.quiet = 1;
        } else if (basic_program_area[basic_program_counter] == ';') // spacer
        {
          basic_program_counter++;
        }
      }
      if (basic_program_area[basic_program_counter - 1] != ';') {
        bus_pirate_configuration.quiet = 0;
        bpBR;
        bus_pirate_configuration.quiet = 1;
      }
      handle_else_statement();
      break;

    case TOK_INPUT:
      pcupdated = 1;
      bus_pirate_configuration.quiet = 0; // print prompt
      basic_program_counter += 4;

      if (basic_program_area[basic_program_counter] == '\"') // is it a string?
      {
        basic_program_counter++;
        while (basic_program_area[basic_program_counter] != '\"') {
          user_serial_transmit_character(basic_program_area[basic_program_counter++]);
        }
        basic_program_counter++;
      }
      if (basic_program_area[basic_program_counter] == ',') {
        basic_program_counter++;
      } else {
        stop = SYNTAXERROR;
      }

      basic_variables[basic_program_area[basic_program_counter] - 'A'] =
          getnumber(0, 0, 0x7FFF, 0);
      basic_program_counter++;
      handle_else_statement();
      bus_pirate_configuration.quiet = ON;
      break;

    case TOK_FOR:
      pcupdated = 1;
      basic_program_counter += 4;
      if (basic_current_nested_for_index < BP_BASIC_NESTED_FOR_LOOP_COUNT) {
        basic_current_nested_for_index++;
      } else {
        stop = FORERROR; // to many nested fors
      }
      basic_nested_for_loops[basic_current_nested_for_index].var =
          (basic_program_area[basic_program_counter++] - 'A') + 1;

      if (basic_program_area[basic_program_counter] == '=') {
        basic_program_counter++;
        basic_variables
            [(basic_nested_for_loops[basic_current_nested_for_index].var) - 1] =
                assign();
      } else {
        stop = SYNTAXERROR;
      }
      if (basic_program_area[basic_program_counter++] ==
          TOK_TO) { // bpWstring(STAT_TO);
        basic_nested_for_loops[basic_current_nested_for_index].to = assign();
      } else {
        stop = SYNTAXERROR;
      }
      if (basic_program_area[basic_program_counter] >= TOK_LEN) {
        basic_nested_for_loops[basic_current_nested_for_index].from =
            basic_program_counter;
      } else {
        stop = SYNTAXERROR;
      }
      handle_else_statement();
      break;

    case TOK_NEXT:
      pcupdated = 1;
      basic_program_counter += 4;

      temp = (basic_program_area[basic_program_counter++] - 'A') + 1;
      stop = NEXTERROR;

      for (i = 0; i <= basic_current_nested_for_index; i++) {
        if (basic_nested_for_loops[i].var == temp) {
          if (basic_variables[temp - 1] < basic_nested_for_loops[i].to) {
            basic_variables[temp - 1]++;
            basic_program_counter = basic_nested_for_loops[i].from;
          } else {
            basic_current_nested_for_index--;
          }
          stop = 0;
        }
      }
      handle_else_statement();
      break;

    case TOK_READ:
      pcupdated = 1;
      basic_program_counter += 4;

      if (basic_data_read_pointer == 0) {
        i = 0;
        while ((basic_program_area[i + 3] != TOK_DATA) &&
               (basic_program_area[i] != 0x00)) {
          i += (basic_program_area[i] - TOK_LEN) + 3;
        }

        if (basic_program_area[i]) {
          basic_data_read_pointer = i + 4;
        } else {
          stop = DATAERROR;
        }
      }
      temp = basic_program_counter;
      basic_program_counter = basic_data_read_pointer;
      basic_variables[basic_program_area[temp] - 'A'] =
          get_number_or_variable();
      basic_data_read_pointer = basic_program_counter;
      basic_program_counter = temp + 1;

      if (basic_program_area[basic_data_read_pointer] == ',') {
        basic_data_read_pointer++;
      }
      if (basic_program_area[basic_data_read_pointer] > TOK_LEN) {
        if (basic_program_area[basic_data_read_pointer + 3] != TOK_DATA) {
          basic_data_read_pointer = 0; // rolover
        } else {
          basic_data_read_pointer += 4;
        }
      }

      handle_else_statement();

      break;
    case TOK_DATA:
      pcupdated = 1;
      basic_program_counter += len + 3;
      break;

    case TOK_START:
      pcupdated = 1;
      basic_program_counter += 4;

      enabled_protocols[bus_pirate_configuration.bus_mode].start();
      handle_else_statement();
      break;

    case TOK_STARTR:
      pcupdated = 1;
      basic_program_counter += 4;

      enabled_protocols[bus_pirate_configuration.bus_mode].start_with_read();
      handle_else_statement();
      break;

    case TOK_STOP:
      pcupdated = 1;
      basic_program_counter += 4;

      enabled_protocols[bus_pirate_configuration.bus_mode].stop();
      handle_else_statement();
      break;

    case TOK_STOPR:
      pcupdated = 1;
      basic_program_counter += 4;

      enabled_protocols[bus_pirate_configuration.bus_mode].stop_from_read();
      handle_else_statement();
      break;

    case TOK_SEND:
      pcupdated = 1;
      basic_program_counter += 4;
      enabled_protocols[bus_pirate_configuration.bus_mode].send((int)assign());
      handle_else_statement();
      break;

    case TOK_AUX:
      pcupdated = 1;
      basic_program_counter += 4;

      if (assign()) {
        bp_aux_pin_set_high();
      } else {
        bp_aux_pin_set_low();
      }
      handle_else_statement();
      break;

    case TOK_PSU:
      pcupdated = 1;
      basic_program_counter += 4;

      if (assign()) {
        BP_VREG_ON();
      } else {
        BP_VREG_OFF();
      }
      handle_else_statement();
      break;

    case TOK_AUXPIN:
      pcupdated = 1;
      basic_program_counter += 4;

      if (assign()) {
        mode_configuration.alternate_aux = ON;
      } else {
        mode_configuration.alternate_aux = OFF;
      }
      handle_else_statement();
      break;

    case TOK_FREQ: {
      int16_t frequency;
      int16_t duty_cycle;
        
      pcupdated = 1;
      basic_program_counter += 4;

      frequency = assign();
      if (frequency < PWM_MINIMUM_FREQUENCY) {
          frequency = PWM_MINIMUM_FREQUENCY;
      }
      if (frequency > PWM_MAXIMUM_FREQUENCY) {
          frequency = PWM_MAXIMUM_FREQUENCY;
      }
      
      duty_cycle = assign();
      if (duty_cycle < PWM_MINIMUM_DUTY_CYCLE) {
          duty_cycle = PWM_MINIMUM_DUTY_CYCLE;
      }
      if (duty_cycle > PWM_MAXIMUM_DUTY_CYCLE) {
          duty_cycle = PWM_MAXIMUM_DUTY_CYCLE;
      }
      
      bp_update_pwm(frequency, duty_cycle);

      handle_else_statement();
      break;
    }

    case TOK_DUTY: {
      int16_t duty_cycle;

      pcupdated = 1;
      basic_program_counter += 4;

      duty_cycle = assign();
      if (duty_cycle < PWM_MINIMUM_DUTY_CYCLE) {
          duty_cycle = PWM_MINIMUM_DUTY_CYCLE;
      }
      if (duty_cycle > PWM_MAXIMUM_DUTY_CYCLE) {
          duty_cycle = PWM_MAXIMUM_DUTY_CYCLE;
      }

      bp_update_duty_cycle(duty_cycle);
      
      handle_else_statement();
      break;
    }

    case TOK_DAT:
      pcupdated = 1;
      basic_program_counter += 4;

      if (assign()) {
        enabled_protocols[bus_pirate_configuration.bus_mode].data_high();
      } else {
        enabled_protocols[bus_pirate_configuration.bus_mode].data_low();
      }
      handle_else_statement();
      break;

    case TOK_CLK:
      pcupdated = 1;
      basic_program_counter += 4;

      switch (assign()) {
      case 0:
        enabled_protocols[bus_pirate_configuration.bus_mode].clock_low();
        break;
      case 1:
        enabled_protocols[bus_pirate_configuration.bus_mode].clock_high();
        break;
      case 2:
        enabled_protocols[bus_pirate_configuration.bus_mode].clock_pulse();
        break;
      }
      handle_else_statement();

      break;
    case TOK_PULLUP:
      pcupdated = 1;
      basic_program_counter += 4;

      if (assign()) {
        BP_PULLUP_ON();
      } else {
        BP_PULLUP_OFF();
      }
      handle_else_statement();
      break;

    case TOK_DELAY:
      pcupdated = 1;
      basic_program_counter += 4;
      temp = assign();
      bp_delay_ms(temp);
      handle_else_statement();

      break;
    case TOK_MACRO:
      pcupdated = 1;
      basic_program_counter += 4;
      temp = assign();
      enabled_protocols[bus_pirate_configuration.bus_mode].run_macro(temp);
      handle_else_statement();
      break;

    case TOK_END:
      stop = 1;
      break;
    default:
      stop = SYNTAXERROR;
      break;
    }

    if (!pcupdated) {
      basic_program_counter += len + 3;
    }
    pcupdated = 0;
  }

  bus_pirate_configuration.quiet = OFF;

  if (stop != NOERROR) {
    // bpWstring("Error(");
    BPMSG1047;
    bp_write_dec_word(stop);
    // bpWstring(") @line:");
    BPMSG1048;
    bp_write_dec_word(lineno);
    // bpWstring(" @pgmspace:");
    BPMSG1049;
    bp_write_dec_word(basic_program_counter);
    bpBR;
  }
}

void list(void) {
  unsigned char c;
  unsigned int lineno;

  basic_program_counter = 0;

  while (basic_program_area[basic_program_counter]) {
    c = basic_program_area[basic_program_counter];
    if (c < TOK_LET) {
      user_serial_transmit_character(c);
    } else if (c > TOK_LEN) {
      bpBR;
      // bpWintdec(pc); bpSP;
      lineno = (basic_program_area[basic_program_counter + 1] << 8) +
               basic_program_area[basic_program_counter + 2];
      basic_program_counter += 2;
      bp_write_dec_word(lineno);
      bpSP;
    } else {
      bpSP;
      bp_write_string(tokens[c - TOKENS]);
      bpSP;
    }
    basic_program_counter++;
  }
  bpBR;
  bp_write_dec_word(basic_program_counter - 1);
  // bpWline(" bytes.");
  BPMSG1050;
}

int compare(char *p) {
  int oldstart;

  oldstart = cmdstart;
  while (*p) {
    if (*p != cmdbuf[cmdstart]) {
      cmdstart = oldstart;
      return 0;
    }
    cmdstart = (cmdstart + 1) & CMDLENMSK;

    p++;
  }
  return 1;
}

unsigned char gettoken(void) {
  int i;

  for (i = 0; i < NUMTOKEN; i++) {
    if (compare(tokens[i])) {
      return TOKENS + i;
    }
  }
  return 0;
}

void bp_basic_enter_interactive_interpreter(void) {
  int i, j, temp;
  int pos, end, len, string;
  unsigned char line[35];

  unsigned int lineno1, lineno2;

  // convert to everyhting to uppercase
  for (i = cmdstart; i != cmdend;) {
    if ((cmdbuf[i] >= 'a') && (cmdbuf[i] <= 'z'))
      cmdbuf[i] &= 0xDF;
    i++;
    i &= CMDLENMSK;
  }

  i = 0;
  // command or a new line?
  if ((cmdbuf[cmdstart] >= '0') &&
      (cmdbuf[cmdstart] <= '9')) { // bpWline("new line");

    for (i = 0; i < 35; i++) {
      line[i] = 0;
    }

    temp = getint();
    line[1] = temp >> 8;
    line[2] = temp & 0xFF;

    // bpWstring("search for line ");
    // bpWintdec(temp); bpBR;

    pos = search_line_number(temp);
    // bpWstring("pos=");
    // bpWintdec(pos); bpSP;
    if (pos != -1) // if it already exist remove it first
    {              // bpWstring("replace/remove line @");
      // bpWintdec(pos); bpBR
      len = (basic_program_area[pos] - TOK_LEN) + 3;
      // bpWstring("pos=");
      // bpWintdec(pos); bpSP;
      for (i = pos; i < BP_BASIC_PROGRAM_SPACE - len; i++) {
        basic_program_area[i] =
            basic_program_area[i +
                               len]; // move everyhting from pos len bytes down
      }
      // bpWstring("i=");
      // bpWintdec(i); bpBR;
      for (; i < BP_BASIC_PROGRAM_SPACE; i++) {
        basic_program_area[i] = 0x00;
      }
    }

    i = 3;
    string = 0;

    consumewhitechars();
    while (cmdstart != cmdend) {
      if (!string) {
        consumewhitechars();
      }

      if (!string) {
        temp = gettoken();
      } else {
        temp = 0;
      }

      if (temp) {
        line[i] = temp;
        if (temp == TOK_REM)
          string = 1; // allow spaces in rem statement
      } else {
        if (cmdbuf[cmdstart] == '"')
          string ^= 0x01;
        line[i] = cmdbuf[cmdstart];
        cmdstart = (cmdstart + 1) & CMDLENMSK;
      }
      i++;
      if (i > 35) { // bpWline("Too long!");
        BPMSG1051;
        return;
      }
    }

    if (i == 3)
      return; // no need to insert an empty line
    if (i == 4)
      return; // no need to insert an empty line

    line[0] = TOK_LEN + (i - 4);

    i = 0;
    end = 0;
    pos = 0;

    while (!end) {
      if (basic_program_area[i] > TOK_LEN) // valid line
      {
        len = basic_program_area[i] - TOK_LEN;
        lineno1 = (basic_program_area[i + 1] << 8) + basic_program_area[i + 2];
        lineno2 = (line[1] << 8) + line[2];
        if (lineno1 < lineno2) {
          pos = i + len + 3;
        }
        i += (len + 3);
      } else {
        end = i; // we found the end! YaY!
      }

      temp = (basic_program_area[i + 1] << 8) + basic_program_area[i + 2];
    }

    temp = (line[0] - TOK_LEN) + 3;

    // for(i=end+temp; i>=pos; i--)
    for (i = end; i >= pos; i--) {
      basic_program_area[i + temp] =
          basic_program_area[i]; // move every thing from pos temp
    }
    for (i = 0; i < temp; i++) // insert line
    {
      basic_program_area[pos + i] = line[i];
    }
  } else {
    if (compare("RUN")) {
      interpreter();
      bpBR;
    } else if (compare("LIST")) {
      list();
    } else if (compare("EXIT")) {
      bus_pirate_configuration.basic = 0;
    }
#ifdef BP_BASIC_I2C_FILESYSTEM
    else if (compare("FORMAT")) {
      format();
    } else if (compare("SAVE")) {
      save();
    } else if (compare("LOAD")) {
      load();
    }
#endif /* BP_BASIC_I2C_FILESYSTEM */
    else if (compare("DEBUG")) {
      for (i = 0; i < BP_BASIC_PROGRAM_SPACE; i += 16) {
        for (j = 0; j < 16; j++) {
          bp_write_hex_byte(basic_program_area[i + j]);
          bpSP;
        }
      }
    } else if (compare("NEW")) {
      bp_basic_initialize();
    } else { // bpWline("Syntax error");
      BPMSG1052;
    }
  }
}

void bp_basic_initialize(void) {
  basic_program_area[0] = TOK_LEN + 1;
  basic_program_area[1] = 0xFF;
  basic_program_area[2] = 0xFF;
  basic_program_area[3] = TOK_END;
  memset(basic_program_area + 4, 0, BP_BASIC_PROGRAM_SPACE - 4);
}

#ifdef BP_BASIC_I2C_FILESYSTEM

// i2c eeprom interaction
// need to incorperate it in bitbang or r2wire!
// CvD: I stole most off it from bitbang.c/h

#define BASSDA 1
#define BASSCL 2
#define BASI2CCLK 100

#define EEP24LC256

#ifdef EEP24LC256

#define I2CADDR 0xA0
#define EEPROMSIZE 0x8000
#define EEPROMPAGE 64

#endif

// globals
int eeprom_lastprog;
unsigned int eeprom_lastmem;

void HIZbbL(unsigned int pins, int delay) {
  IOLAT &= (~pins);   // pins to 0
  IODIR &= (~pins);   // direction to output
  bp_delay_us(delay); // delay
}
void HIZbbH(unsigned int pins, int delay) {
  IODIR |= pins;      // open collector output high
  bp_delay_us(delay); // delay
}
unsigned char HIZbbR(unsigned int pin) {
  IODIR |= pin; // pin as input
  Nop();
  Nop();
  Nop();
  if (IOPOR & pin)
    return 1;
  else
    return 0; // clear all but pin bit and return result
}

void basi2cstart(void) {
  HIZbbH((BASSDA | BASSCL), BASI2CCLK); // both hi
  HIZbbL(BASSDA, BASI2CCLK);            // data down
  HIZbbL(BASSCL, BASI2CCLK);            // clk down
  HIZbbH(BASSDA, BASI2CCLK);            // data up
}

void basi2cstop(void) {
  HIZbbL((BASSDA | BASSCL), BASI2CCLK);
  HIZbbH(BASSCL, BASI2CCLK);
  HIZbbH(BASSDA, BASI2CCLK);
}

unsigned char basi2cread(int ack) {
  int i;
  unsigned char c;

  c = 0;
  HIZbbR(BASSDA);

  for (i = 0; i < 8; i++) {
    HIZbbL(BASSCL, BASI2CCLK / 5);
    HIZbbH(BASSCL, BASI2CCLK);
    c <<= 1;
    c |= HIZbbR(BASSDA);

    HIZbbL(BASSCL, BASI2CCLK);
  }

  if (ack) {
    HIZbbL(BASSDA, BASI2CCLK / 5);
  }
  HIZbbH(BASSCL, BASI2CCLK);
  HIZbbL(BASSCL, BASI2CCLK);

  return c;
}

int basi2cwrite(unsigned char c) {
  int i;
  unsigned char mask;

  mask = 0x80;

  for (i = 0; i < 8; i++) {
    if (c & mask) {
      HIZbbH(BASSDA, BASI2CCLK / 5);
      // bpWstring("W1");
    } else {
      HIZbbL(BASSDA, BASI2CCLK / 5);
      // bpWstring("W0");
    }
    HIZbbH(BASSCL, BASI2CCLK);
    HIZbbL(BASSCL, BASI2CCLK);
    mask >>= 1;
  }

  HIZbbH(BASSCL, BASI2CCLK);
  i = HIZbbR(BASSDA);
  HIZbbL(BASSCL, BASI2CCLK);

  return (i ^ 0x01);
}

int checkeeprom(void) { // just to be sure
  basi2cstop();
  basi2cstop();
  basi2cstart();
  if (!basi2cwrite(I2CADDR)) { // bpWline("No EEPROM");
    BPMSG1053;
    return 0;
  }
  basi2cwrite(0x00);
  basi2cwrite(0x00);
  basi2cstart();
  if (basi2cread(1) == 0x00) // check for any data
  {
    bp_write_line("No EEPROM"); // if 0 prolly no pullup and eeprom (PROLLY!)
    BPMSG1053;
    return 0;
  }
  basi2cstop();
  return 1;
}

void format(void) {
  int i, j;

  basi2cstop();
  basi2cstart();
  if (!basi2cwrite(I2CADDR)) { // bpWline("No EEPROM");
    BPMSG1053;
    return;
  }
  basi2cstop();

  // bpWstring("Erasing");
  BPMSG1054;
  for (i = 0; i < EEPROMSIZE; i += EEPROMPAGE) {
    basi2cstart();
    basi2cwrite(I2CADDR);
    basi2cwrite((i >> 8));
    basi2cwrite((i & 0x0FF));
    for (j = 0; j < EEPROMPAGE; j++) {
      basi2cwrite(0xFF);
    }
    basi2cstop();
    user_serial_transmit_character('.');
    waiteeprom();
  }
  // bpWline("done");
  BPMSG1055;
}

void waiteeprom(void) {
  int wait;
  wait = 1;
  while (wait) {
    basi2cstart();
    wait = basi2cwrite(I2CADDR);
    basi2cstop();
  }
}

void save(void) {
  int i, j;
  int slot;

  consumewhitechars();
  slot = getint();

  if (slot == 0) { // bpWline("Syntax error");
    BPMSG1052;
    return;
  }

  // bpWstring("Saving to slot ");
  BPMSG1056;
  bp_write_dec_byte(slot);
  bpBR;

  if (slot > (EEPROMSIZE / PGMSIZE)) { // bpWline("Invalid slot");
    BPMSG1057;
    return;
  }

  if (!checkeeprom()) {
    return;
  }

  slot *= PGMSIZE;

  basi2cstop();
  basi2cwrite(I2CADDR);
  basi2cwrite(slot >> 8);
  basi2cwrite(slot & 0x0FF);
  basi2cstart();
  basi2cwrite(I2CADDR + 1);

  slot *= EEPROMPAGE;

  for (i = 0; i < PGMSIZE;
       i += EEPROMPAGE) // we assume that pgmsize is dividable by eeprompage
  {
    basi2cstart();
    basi2cwrite(I2CADDR);
    basi2cwrite((slot + i) >> 8);
    basi2cwrite((slot + i) & 0x0FF);
    for (j = 0; j < EEPROMPAGE; j++) {
      basi2cwrite(basic_program_area[i + j]);
    }
    basi2cstop();
    user_serial_transmit_character('.');
    waiteeprom();
  }
}

void load(void) {
  int i;
  int slot;

  consumewhitechars();
  slot = getint();

  if (slot == 0) { // bpWline("Syntax error");
    BPMSG1052;
    return;
  }

  // bpWstring("Loading from slot ");
  BPMSG1058;
  bp_write_dec_byte(slot);
  bpBR;

  if (slot > (EEPROMSIZE / PGMSIZE)) { // bpWline("Invalid slot");
    BPMSG1057;
    return;
  }

  if (!checkeeprom()) {
    return;
  }

  slot *= PGMSIZE;

  basi2cstop();
  basi2cwrite(I2CADDR);
  basi2cwrite(slot >> 8);
  basi2cwrite(slot & 0x0FF);
  basi2cstart();
  basi2cwrite(I2CADDR + 1);

  for (i = 0; i < PGMSIZE; i++) {
    if (!(i % EEPROMPAGE))
      user_serial_transmit_character('.'); // pure estetic
    basic_program_area[i] = basi2cread(1);
  }
}

#endif /* BP_BASIC_I2C_FILESYSTEM */
#endif /* BP_ENABLE_BASIC_SUPPORT */