/*
 * SNESDev - Simulates a virtual keyboard for two SNES controllers that are 
 * connected to the GPIO pins of the Raspberry Pi.
 *
 * (c) Copyright 2012  Florian Müller (petrockblog@flo-mueller.com)
 *
 * SNESDev homepage: https://github.com/petrockblog/SNESDev-RPi
 *
 * Permission to use, copy, modify and distribute SNESDev in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * SNESDev is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for SNESDev or software derived from SNESDev.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so everyone can benefit from the modifications
 * in future versions.
 *
 * Raspberry Pi is a trademark of the Raspberry Pi Foundation.
 */

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bcm2835.h>
#include <signal.h>
#include <time.h>

#include "SNESpad.h"
#include "cpuinfo.h"

/* time to wait after each cycle */
#define FRAMEWAIT 20
#define FRAMEWAITLONG 100

/* set the GPIO pins of the button and the LEDs. */
#define BUTTONPIN     RPI_GPIO_P1_11
#define BUTTONPIN_V2  RPI_V2_GPIO_P1_11
#define BTNSTATE_IDLE 0
#define BTNSTATE_UP_1 1
#define BTNSTATE_PRESS_1 2
#define BTNSTATE_UP_2 3
#define BTNSTATE_PRESS_2 4

int uinp_fd;
int doRun, pollButton, pollPads;
time_t btnLastTime;
uint8_t btnState;
int buttonPin;

/* Signal callback function */
void sig_handler(int signo) {
  if ((signo == SIGINT) | (signo == SIGKILL) | (signo == SIGQUIT) | (signo == SIGABRT)) {
  	printf("Releasing SNESDev-Rpi device.\n");
  	pollButton = 0;
  	pollPads = 0;
	/* Destroy the input device */
	ioctl(uinp_fd, UI_DEV_DESTROY);
	/* Close the UINPUT device */
	close(uinp_fd);  	
	doRun = 0;
  }
}

/* Setup the uinput device */
int setup_uinput_device() {
	int uinp_fd = open("/dev/uinput", O_WRONLY | O_NDELAY);
	if (uinp_fd == 0) {
		printf("Unable to open /dev/uinput\n");
		return -1;
	}

	struct uinput_user_dev uinp;
	memset(&uinp, 0, sizeof(uinp)); 
	strncpy(uinp.name, "SNES-to-Keyboard Device", strlen("SNES-to-Keyboard Device"));
	uinp.id.version = 4;
	uinp.id.bustype = BUS_USB;
	uinp.id.product = 1;
	uinp.id.vendor = 1;

	// Setup the uinput device
	ioctl(uinp_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinp_fd, UI_SET_EVBIT, EV_REL);
	int i = 0;
	for (i = 0; i < 256; i++) {
		ioctl(uinp_fd, UI_SET_KEYBIT, i);
	}

	/* Create input device into input sub-system */
	write(uinp_fd, &uinp, sizeof(uinp));
	if (ioctl(uinp_fd, UI_DEV_CREATE)) {
		printf("[SNESDev-Rpi] Unable to create UINPUT device.");
		return -1;
	}

	return uinp_fd;
}

/* sends a key event to the virtual device */
void send_key_event(int fd, unsigned int keycode, int keyvalue) {
	struct input_event event;
	gettimeofday(&event.time, NULL);

	event.type = EV_KEY;
	event.code = keycode;
	event.value = keyvalue;

	if (write(fd, &event, sizeof(event)) < 0) {
		printf("[SNESDev-Rpi] Simulate key error\n");
	}

	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(fd, &event, sizeof(event));
	if (write(fd, &event, sizeof(event)) < 0) {
		printf("[SNESDev-Rpi] Simulate key error\n");
	}
}

/* checks the state of the button and decides for a short or long button press */
void checkButton(int uinh) {
  
  	// read the state of the button into a local variable
	uint8_t buttonPosition = bcm2835_gpio_lev(buttonPin);
  
  	// 5-state machine:
  	// - press and release one times: send "H"
  	// - press and release two times: send "Escape"
	switch ( buttonPosition ) {
		case BTNSTATE_IDLE:
			if ( buttonPosition == HIGH ) {
				btnState = BTNSTATE_UP_1;
				printf("In idle, changing to up 1");
			}
			break;
		case BTNSTATE_UP_1:
			if ( buttonPosition == LOW ) {
				btnLastTime = time(NULL);
				btnState = BTNSTATE_PRESS_1;
				printf("In up 1, changing to press 1");
			}
			break;
		case BTNSTATE_PRESS_1:
		 	if ( difftime(time(NULL),btnLastTime) > 1 ) {
				// Reset current game
				send_key_event(uinh, KEY_H, 1);
				usleep(50000); // 0.05s
				send_key_event(uinh, KEY_H, 0);
				btnState = BTNSTATE_IDLE;
				printf("Sent h, returning to idle");
			} else if ( buttonPosition == HIGH ) {
				btnState = BTNSTATE_UP_2;
				printf("In press 1, changing to up 2");
			}
			break;
		case BTNSTATE_UP_2:
			if ( buttonPosition == LOW ) {
				btnLastTime = time(NULL);
				btnState = BTNSTATE_PRESS_2;
				printf("In up 2, changing to press 2");
			}
			break;
		case BTNSTATE_PRESS_2:
		 	if ( difftime(time(NULL),btnLastTime) > 1 ) {
				// Return to ES
				send_key_event(uinh, KEY_ESC,1);
				usleep(50000);
				send_key_event(uinh, KEY_ESC,0);
				btnState = BTNSTATE_IDLE;
				printf("Sent esc, returning to idle");
			}
			break;
	}
	// printf("State: %d \n",btnState );
}

/* checks, if a button on the pad is pressed and sends an event according the button state. */
void processPadBtn(uint16_t buttons, uint16_t mask, uint16_t key, int uinh) {
	if ( (buttons & mask) == mask ) {
		send_key_event(uinh, key, 1);
	} else {
		send_key_event(uinh, key, 0);
	}
}


int main(int argc, char *argv[]) {

    buttonstates padButtons;
	snespad pads;
	pollButton = 1;
	pollPads = 1;
	doRun = 1;

	btnState = BTNSTATE_IDLE;

    if (get_rpi_revision()==1) {
    	buttonPin = BUTTONPIN;
    } else {
		buttonPin = BUTTONPIN_V2;
    }


	// check command line arguments
	if (argc > 1) {
		// argv[1]==1 poll controllers only
		// argv[1]==2 poll button only
		// argv[1]==3 poll controllers and button
		switch ( atoi(argv[argc-1]) ) {
			case 1:
				printf("[SNESDev-Rpi] Polling only controllers.\n");
				pollButton = 0;
				pollPads = 1;
			break;
			case 2:
				printf("[SNESDev-Rpi] Polling button only.\n");
				pollButton = 1;
				pollPads = 0;
			break;
			case 3:
				printf("[SNESDev-Rpi] Polling controllers and button.\n");
				pollButton = 1;
				pollPads = 1;
			break;
			default:
				return -1;
		}
    } else {
    	printf("[SNESDev-Rpi] Polling controllers and button.\n");
    }

    if (!bcm2835_init())
        return 1;

	// initialize button and LEDs
    bcm2835_gpio_fsel(buttonPin,  BCM2835_GPIO_FSEL_INPT);

    /* initialize controller structures with GPIO pin assignments */

    // check board revision and set pins to be used
    // these are acutally also used by the gamecon driver
    if (get_rpi_revision()==1)
    {
		pads.clock  = RPI_GPIO_P1_19;
		pads.strobe = RPI_GPIO_P1_23;
		pads.data1  = RPI_GPIO_P1_07;
		pads.data2  = RPI_GPIO_P1_05;
    } else {
		pads.clock  = RPI_V2_GPIO_P1_19;
		pads.strobe = RPI_V2_GPIO_P1_23;
		pads.data1  = RPI_V2_GPIO_P1_07;
		pads.data2  = RPI_V2_GPIO_P1_05;
    }

	/* set GPIO pins as input or output pins */
	initializePads( &pads );

	/* intialize virtual input device */
	if ((uinp_fd = setup_uinput_device()) < 0) {
		printf("[SNESDev-Rpi] Unable to find uinput device\n");
		return -1;
	}

	if (signal(SIGINT, sig_handler) == SIG_ERR)	printf("\n[SNESDev-Rpi] Cannot catch SIGINT\n");
	if (signal(SIGQUIT, sig_handler) == SIG_ERR) printf("\n[SNESDev-Rpi] Cannot catch SIGQUIT\n");
	if (signal(SIGABRT, sig_handler) == SIG_ERR) printf("\n[SNESDev-Rpi] Cannot catch SIGABRT\n");

	/* enter the main loop */
	while ( doRun ) {

		if (pollButton) {
			/* Check state of button. */
			checkButton(uinp_fd);
		}

		if (pollPads) {
			/* read states of the buttons */
			updateButtons(&pads, &padButtons);

			/* send an event (pressed or released) for each button */
			/* key events for first controller */
	        processPadBtn(padButtons.buttons1, SNES_A,     KEY_X,          uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_B,     KEY_Z,          uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_SELECT,KEY_RIGHTSHIFT, uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_START, KEY_ENTER,      uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_LEFT,  KEY_LEFT,       uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_RIGHT, KEY_RIGHT,      uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_UP,    KEY_UP,         uinp_fd);
	        processPadBtn(padButtons.buttons1, SNES_DOWN,  KEY_DOWN,       uinp_fd);

			// key events for second controller 
	        processPadBtn(padButtons.buttons2, SNES_A,     KEY_E, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_B,     KEY_R, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_SELECT,KEY_O, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_START, KEY_P, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_LEFT,  KEY_C, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_RIGHT, KEY_B, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_UP,    KEY_F, uinp_fd);
	        processPadBtn(padButtons.buttons2, SNES_DOWN,  KEY_V, uinp_fd);
		}

		/* wait for some time to keep the CPU load low */
		if (pollButton && !pollPads) {
			delay(FRAMEWAITLONG);
		} else {
			delay(FRAMEWAIT);
		}
	}

	return 0;
}
