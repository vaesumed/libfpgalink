/*
 * Copyright (C) 2009-2012 Chris McClelland
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <fx2regs.h>
#include <fx2macros.h>
#include <eputils.h>
#include <delay.h>
#include <setupdat.h>
#include <makestuff.h>
#include "../../vendorCommands.h"
#include "prom.h"
#include "prog.h"
#include "defs.h"
#include "debug.h"

void livePatch(uint8 patchClass, uint8 newByte);

// General-purpose diagnostic code, for debugging. See CMD_GET_DIAG_CODE vendor command.
xdata uint8 m_diagnosticCode = 0;

xdata uint8 setIntCount;

void fifoSetEnabled(bool enabled) {
	if ( enabled ) {
		IFCONFIG = (bmIFCLKSRC | bm3048MHZ | bmIFCLKOE | bmFIFOS);
	} else {
		IFCONFIG = (bmIFCLKSRC | bm3048MHZ | bmIFCLKOE | bmPORTS);
	}
}

// Called once at startup
//
void mainInit(void) {

	xdata uint8 thisByte = 0xFF;
	xdata uint16 blockSize;

	setIntCount = 23;

	// This is only necessary for cases where you want to load firmware into the RAM of an FX2 that
	// has already loaded firmware from an EEPROM. It should definitely be removed for firmwares
	// which are themselves to be loaded from EEPROM.
#ifndef EEPROM
	RENUMERATE_UNCOND();
#endif

	// Disable alternate functions for PORTA 0,1,3 & 7.
	PORTACFG = 0x00;

	// Return FIFO setings back to default just in case previous firmware messed with them.
	SYNCDELAY; PINFLAGSAB = 0x00;
	SYNCDELAY; PINFLAGSCD = 0x00;
	SYNCDELAY; FIFOPINPOLAR = 0x00;

	// Global settings
	SYNCDELAY; REVCTL = (bmDYN_OUT | bmENH_PKT);
	SYNCDELAY; CPUCS = bmCLKSPD1;  // 48MHz

	// Drive IFCLK at 48MHz, enable slave FIFOs
	//SYNCDELAY; IFCONFIG = (bmIFCLKSRC | bm3048MHZ | bmIFCLKOE | bmFIFOS);
	SYNCDELAY; IFCONFIG = (bmIFCLKSRC | bm3048MHZ | bmIFCLKOE | bmPORTS);

	// EP4 & EP8 are unused
	SYNCDELAY; EP4CFG = 0x00;
	SYNCDELAY; EP8CFG = 0x00;
	SYNCDELAY; EP4FIFOCFG = 0x00;
	SYNCDELAY; EP8FIFOCFG = 0x00;

	// EP1OUT & EP1IN
	SYNCDELAY; EP1OUTCFG = (bmVALID | bmBULK);
	SYNCDELAY; EP1INCFG = (bmVALID | bmBULK);

	// EP2OUT & EP6IN are quad-buffered bulk endpoints
	SYNCDELAY; EP2CFG = (bmVALID | bmBULK);
	SYNCDELAY; EP6CFG = (bmVALID | bmBULK | bmDIR);

	// Reset FIFOs for EP2OUT & EP6IN
	SYNCDELAY; FIFORESET = bmNAKALL;
	SYNCDELAY; FIFORESET = 2;  // reset EP2OUT
	SYNCDELAY; FIFORESET = 6;  // reset EP6IN
	SYNCDELAY; FIFORESET = 0x00;

	// Arm EP1OUT
	EP1OUTBC = 0x00;

	// Arm the EP2OUT buffers. Done four times because it's quad-buffered
	SYNCDELAY; OUTPKTEND = bmSKIP | 2;  // EP2OUT
	SYNCDELAY; OUTPKTEND = bmSKIP | 2;
	SYNCDELAY; OUTPKTEND = bmSKIP | 2;
	SYNCDELAY; OUTPKTEND = bmSKIP | 2;

	// EP2OUT & EP6IN automatically commit packets
	SYNCDELAY; EP2FIFOCFG = bmAUTOOUT;
	SYNCDELAY; EP6FIFOCFG = bmAUTOIN;

	// Auto-commit 512-byte packets from EP6IN (master may commit early by asserting PKTEND)
	SYNCDELAY; EP6AUTOINLENH = 0x02;
	SYNCDELAY; EP6AUTOINLENL = 0x00;
	
	// Turbo I2C
	I2CTL |= bm400KHZ;

	// Auto-pointers
	AUTOPTRSETUP = bmAPTREN | bmAPTR1INC | bmAPTR2INC;

	// Port lines all inputs...
	IOA = 0xFF;
	OEA = 0x00;
	IOB = 0xFF;
	OEB = 0x00;
	IOC = 0xFF;
	OEC = 0x00;
	IOD = 0xFF;
	OED = 0x00;
	IOE = 0xFF;
	OEE = 0x00;

#ifdef EEPROM
	#include "init.inc"
#endif

#ifdef DEBUG
	usartInit();
	usartSendString("MakeStuff FPGALink/FX2 v1.1\r");
#endif
}

// Called repeatedly while the device is idle
//
void mainLoop(void) {
	// If there is a shift operation pending, execute it now.
	progShiftExecute();
}

#define MODE_FIFO (1<<1)

uint8 handle_set_interface(uint8 ifc, uint8 alt);

#define updateRegister(reg, val) tempByte = reg; tempByte &= ~mask; tempByte |= val; reg = tempByte

uint8 portAccess(uint8 portSelect, uint8 mask, uint8 ddrWrite, uint8 portWrite) {
	xdata uint8 tempByte = 0x00;
	switch ( portSelect ) {
	case 0:
		updateRegister(IOA, portWrite);
		updateRegister(OEA, ddrWrite);
		tempByte = IOA;
		break;
	case 1:
		updateRegister(IOB, portWrite);
		updateRegister(OEB, ddrWrite);
		tempByte = IOB;
		break;
	case 2:
		updateRegister(IOC, portWrite);
		updateRegister(OEC, ddrWrite);
		tempByte = IOC;
		break;
	case 3:
		updateRegister(IOD, portWrite);
		updateRegister(OED, ddrWrite);
		tempByte = IOD;
		break;
	case 4:
		updateRegister(IOE, portWrite);
		updateRegister(OEE, ddrWrite);
		tempByte = IOE;
		break;
	}
	return tempByte;
}

uint8 tryReset(void);

// Called when a vendor command is received
//
uint8 handleVendorCommand(uint8 cmd) {
	switch(cmd) {

	// Set various mode bits, or fetch status information
	//
	case CMD_MODE_STATUS:
		if ( SETUP_TYPE == (REQDIR_HOSTTODEVICE | REQTYPE_VENDOR) ) {
			xdata uint16 wBits = SETUP_VALUE();
			xdata uint16 wMask = SETUP_INDEX();
			if ( wMask & MODE_FIFO ) {
				// Enable or disable FIFO mode
				fifoSetEnabled(wBits & MODE_FIFO ? true : false);
			} else {
				return false;
			}
		} else {
			// Get STATUS: return the diagnostic byte
			while ( EP0CS & bmEPBUSY );
			EP0BUF[0] = 'N';                     // Magic bytes (my cat's name)
			EP0BUF[1] = 'E';
			EP0BUF[2] = 'M';
			EP0BUF[3] = 'I';
			EP0BUF[4] = m_diagnosticCode;        // Last operation diagnostic code
			EP0BUF[5] = (IOA & bmBIT2) ? 0 : 1;  // Flags
			EP0BUF[6] = 0x11;                    // NeroProg endpoints
			EP0BUF[7] = 0x26;                    // CommFPGA endpoints
			EP0BUF[8] = 0x00;                    // Reserved
			EP0BUF[9] = 0x00;                    // Reserved
			EP0BUF[10] = 0x00;                   // Reserved
			EP0BUF[11] = 0x00;                   // Reserved
			EP0BUF[12] = 0x00;                   // Reserved
			EP0BUF[13] = 0x00;                   // Reserved
			EP0BUF[14] = 0x00;                   // Reserved
			EP0BUF[15] = 0x00;                   // Reserved
			
			// Return status packet to host
			EP0BCH = 0;
			SYNCDELAY;
			EP0BCL = 16;
		}
		return true;

	// Clock data into and out of the JTAG chain. Reads from EP2OUT and writes to EP4IN.
	//
	case CMD_JTAG_CLOCK_DATA:
		if ( SETUP_TYPE == (REQDIR_HOSTTODEVICE | REQTYPE_VENDOR) ) {
			EP0BCL = 0x00;                                     // Allow host transfer in
			while ( EP0CS & bmEPBUSY );                        // Wait for data
			progShiftBegin(*((uint32 *)EP0BUF), (ProgOp)SETUPDAT[4], SETUPDAT[2]);  // Init numBits & flagByte
			return true;
			// Now that numBits & flagByte are set, this operation will continue in mainLoop()...
		}
		break;
		
	// Clock an (up to) 32-bit pattern LSB-first into TMS to change JTAG TAP states
	//
	case CMD_JTAG_CLOCK_FSM:
		if ( SETUP_TYPE == (REQDIR_HOSTTODEVICE | REQTYPE_VENDOR) ) {
			EP0BCL = 0x00;                                   // Allow host transfer in
			while ( EP0CS & bmEPBUSY );                      // Wait for data
			progClockFSM(*((uint32 *)EP0BUF), SETUPDAT[2]);  // Bit pattern, transitionCount
			return true;
		}
		break;
		
	// Execute a number of JTAG clocks.
	//
	case CMD_JTAG_CLOCK:
		if ( SETUP_TYPE == (REQDIR_HOSTTODEVICE | REQTYPE_VENDOR) ) {
			progClocks(*((uint32 *)(SETUPDAT+2)));
			return true;
		}
		break;

	// Set various mode bits, or fetch status information
	//
	case CMD_PORT_IO:
		if ( SETUP_TYPE == (REQDIR_DEVICETOHOST | REQTYPE_VENDOR) ) {
			const xdata uint8 portSelect = SETUPDAT[4];
			const xdata uint8 mask = SETUPDAT[5];
			xdata uint8 ddrWrite = SETUPDAT[2];
			xdata uint8 portWrite = SETUPDAT[3];

			//usartSendString("Got: ");
			//usartSendByteHex(portSelect);
			//usartSendByteHex(mask);
			//usartSendByteHex(ddrWrite);
			//usartSendByteHex(portWrite);
			//usartSendByte('\r');

			if ( portSelect > 4 ) {
				return false;  // illegal port
			}
			portWrite &= mask;
			ddrWrite &= mask;

			// Get the state of the port lines:
			while ( EP0CS & bmEPBUSY );
			EP0BUF[0] = portAccess(portSelect, mask, ddrWrite, portWrite);
			EP0BCH = 0;
			SYNCDELAY;
			EP0BCL = 1;
			return true;
		}
		break;

	case CMD_PORT_MAP:
		if ( SETUP_TYPE == (REQDIR_HOSTTODEVICE | REQTYPE_VENDOR) ) {
			const xdata uint8 patchClass = SETUPDAT[4];
			const xdata uint8 patchPort = SETUPDAT[5];
			if ( patchClass < 4 ) {
				const xdata uint8 patchBit = SETUPDAT[2];
				livePatch(patchClass, 0x80 + (patchPort << 4) + patchBit);
			} else {
				livePatch(
					patchClass, 
					0x80 + (patchPort << 4)
				);
			}
			return true;
		}
		break;

	// Command to talk to the EEPROM
	//
	case CMD_READ_WRITE_EEPROM:
		if ( SETUP_TYPE == (REQDIR_DEVICETOHOST | REQTYPE_VENDOR) ) {
			// It's an IN operation - read from prom and send to host
			xdata uint16 address = SETUP_VALUE();
			xdata uint16 length = SETUP_LENGTH();
			xdata uint16 chunkSize;
			xdata uint8 i;
			while ( length ) {
				while ( EP0CS & bmEPBUSY );
				chunkSize = length < EP0BUF_SIZE ? length : EP0BUF_SIZE;
				for ( i = 0; i < chunkSize; i++ ) {
					EP0BUF[i] = 0x23;
				}
				promRead(SETUPDAT[4], address, chunkSize, EP0BUF);
				EP0BCH = 0;
				SYNCDELAY;
				EP0BCL = chunkSize;
				address += chunkSize;
				length -= chunkSize;
			}
		} else if ( SETUP_TYPE == (REQDIR_HOSTTODEVICE | REQTYPE_VENDOR) ) {
			// It's an OUT operation - read from host and send to prom
			xdata uint16 address = SETUP_VALUE();
			xdata uint16 length = SETUP_LENGTH();
			xdata uint16 chunkSize;
			while ( length ) {
				EP0BCL = 0x00; // allow pc transfer in
				while ( EP0CS & bmEPBUSY ); // wait for data
				chunkSize = EP0BCL;
				promWrite(SETUPDAT[4], address, chunkSize, EP0BUF);
				address += chunkSize;
				length -= chunkSize;
			}
		}
		return true;
	}
	return false;  // unrecognised command
}
