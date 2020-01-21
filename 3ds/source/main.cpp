﻿//3DS Smash Custom Music Client
//Copyright (C) 2020 Extrasklep
#include <iostream>
#include <fstream>
#include <cstring>
#include <math.h>
#include <3ds.h>

#include "audio.h"
#include "http.h"

char* swkb_buf;
char* getSwkbText(const char* hint) {
    delete[] swkb_buf;
    swkb_buf = new char[255];
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 255);
    swkbdSetHintText(&swkbd, hint);
    swkbdInputText(&swkbd, swkb_buf, 255);
    return swkb_buf;
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    brstmInit();
    
    /*Result rc = romfsInit();
    if (rc) printf("romfsInit: %08lX\n", rc);
    else {
        printf("romfs Init Successful!\n");
        printfile("romfs:/folder/file.txt");
        // Test reading a file with non-ASCII characters in the name
        printfile("romfs:/フォルダ/ファイル.txt");
    }*/
    
    //Get the bottom screen's frame buffer
    uint16_t xresfbb = 320;
    uint16_t yresfbb = 240;
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
	uint8_t *fbb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &xresfbb, &yresfbb);
    
    /*//Get the top screen's frame buffer
    uint16_t xresfbt = 400;
    uint16_t yresfbt = 240;
    gfxSetDoubleBuffering(GFX_TOP, false);
	uint8_t *fbt = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &xresfbt, &yresfbt);
    */
    uint8_t colorOffsetCounter = 0;
    
    std::cout << "Hello from C++!\nPress A to play a brstm\nPress B to stop it\nPress X to pause it\n";
    
    
    //temp
    bool loadingBarStatusPos = false;
    
    // Main loop
    while (aptMainLoop()) {
        
        //Get input
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {break;} // break in order to return to hbmenu
        
        if (kDown & KEY_A) {
            playBrstm(getSwkbText("Enter brstm filename"));
        }
        if(brstmBeingRead) {
            //show loading status
            const char* loadingbar = (loadingBarStatusPos=!loadingBarStatusPos) ? "- " : " -";
            std::cout << "\r[" << loadingbar << "] Loading BRSTM ";
        }
        if(brstmDoneReading) {
            brstmDoneReading = false;
            unsigned char res = brstm_readfile_res;
            if(res) {
                if(res<10) {
                    std::cout << "BRSTM Error: Unable to open file\n";
                } else if(res<50) {
                    std::cout << "BRSTM Error: Not enough memory\n";
                } else if(res<60) {
                    std::cout << "BRSTM Error: Thread error\n";
                } else if(res<128) {
                    std::cout << "BRSTM Error: NDSP Init error\n";
                } else {
                    std::cout << "BRSTM Error: BRSTM read error\n";
                }
            }
        }
        if (kDown & KEY_B) {
            stopBrstm();
        }
        if (kDown & KEY_X) {
            brstm_togglepause();
        }
        if(kDown & KEY_LEFT) {
            brstm_seek(-HEAD1_sample_rate);
        }
        if(kDown & KEY_RIGHT) {
            brstm_seek(HEAD1_sample_rate);
        }
        
        //Draw colors on bottom screen
        for(unsigned int y=0;y<yresfbb;y++) {
            for(unsigned int x=0;x<yresfbb;x++) {
                fbb[(y*xresfbb+x)*3  ] = x/2+colorOffsetCounter;
                fbb[(y*xresfbb+x)*3+1] = y/2+colorOffsetCounter;
                fbb[(y*xresfbb+x)*3+2] = x/2+y/2;
            }
        }
        colorOffsetCounter++;
        
        //Flush buffers and wait for vblank
        gspWaitForVBlank();
        gfxFlushBuffers();
        gfxSwapBuffers();
    }
    
    brstmExit();
    //romfsExit();
    gfxExit();
    return 0;
}
