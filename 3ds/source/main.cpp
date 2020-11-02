//3DS BRSTM and SmashCustomMusic experiments
//Copyright (C) 2020 I.C.
#include <iostream>
#include <fstream>
#include <cstring>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <malloc.h>
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

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    audio_brstm_init();
    aptInit();
    
    int res = 0;
    
    //Initialize socket service
    uint32_t* SOC_buffer = (uint32_t*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    
    if(SOC_buffer == NULL) {
        std::cout << "Failed to initalize socket service.\n";
        exit(1);
    }
    //Now intialise soc:u service
    if((res = socInit(SOC_buffer, SOC_BUFFERSIZE)) != 0) {
        std::cout << "Failed to initalize socket service.\nsocInit: 0x" << std::hex << res << std::dec << "\n";
        exit(1);
    }
    
    if((res = http_init())) {
        std::cout << "CURL error " << res << '\n';
    }
    
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
    
    // Main loop
    while (aptMainLoop()) {
        
        //Get input
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {break;} // break in order to return to hbmenu
        
        if (kDown & KEY_A) {
            unsigned char res;
            
            //Make correct URL with brstm ID
            char url[1024];
            strcpy(url, "https://smashcustommusic.net/brstm/");
            const char* kb_input = getSwkbText("Enter brstm ID");
            strcat(url, kb_input);
            strcat(url, "&noIncrement=1");
            
            res = http_downloadfile(url ,"/scm.brstm");
            if(res) {
                std::cout << "Download fail\n";
            }
            
            res = audio_brstm_play("/scm.brstm");
            if(res) {
                if(res<10) {
                    std::cout << "BRSTM Error: Unable to open file\n";
                } else if(res<50) {
                    std::cout << "BRSTM Error: Not enough memory\n";
                } else if(res<128) {
                    std::cout << "BRSTM Error: NDSP Init error\n";
                } else {
                    std::cout << "BRSTM Error: BRSTM read error\n";
                }
            }
        }
        if (kDown & KEY_B) {
            audio_brstm_stop();
        }
        if (kDown & KEY_X) {
            audio_brstm_togglepause();
        }
        if(kDown & KEY_LEFT) {
            audio_brstm_seek(0-audio_brstm_s->sample_rate);
        }
        if(kDown & KEY_RIGHT) {
            audio_brstm_seek(audio_brstm_s->sample_rate);
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
    
    socExit();
    audio_brstm_exit();
    http_exit();
    //romfsExit();
    gfxExit();
    aptExit();
    return 0;
}
