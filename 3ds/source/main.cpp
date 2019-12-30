#include <iostream>
#include <cstring>
#include <math.h>
#include <3ds.h>

//audio stuff
void audio_fillbuffer(void *audioBuffer,size_t offset,size_t size,int frequency);

unsigned int audio_samplerate = 32000;
unsigned int audio_samplesperbuf = (audio_samplerate / 15);
unsigned int audio_bytespersample = 4;

bool fillBlock = false;
float mix[12];
ndspWaveBuf waveBuf[2];
size_t stream_offset = 0;
u32 *audioBuffer;

void audio_init() {
    fillBlock=false;
    
    audioBuffer = (u32*)linearAlloc(audio_samplesperbuf*audio_bytespersample*2);
    
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, audio_samplerate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0;
    mix[1] = 1.0;
    ndspChnSetMix(0, mix);
    
    memset(waveBuf,0,sizeof(waveBuf));
    waveBuf[0].data_vaddr = &audioBuffer[0];
    waveBuf[0].nsamples = audio_samplesperbuf;
    waveBuf[1].data_vaddr = &audioBuffer[audio_samplesperbuf];
    waveBuf[1].nsamples = audio_samplesperbuf;
    
    audio_fillbuffer(audioBuffer,stream_offset, audio_samplesperbuf * 2, 400);
    stream_offset += audio_samplesperbuf;
    ndspChnWaveBufAdd(0, &waveBuf[0]);
    ndspChnWaveBufAdd(0, &waveBuf[1]);
}

void audio_deinit() {
    ndspExit();
    linearFree(audioBuffer);
}

void audio_fillbuffer(void *audioBuffer,size_t offset,size_t size,int frequency) {
    uint32_t *dest = (uint32_t*)audioBuffer;
    
    for (unsigned int i=0; i<size; i++) {
        int16_t sample = INT16_MAX * sin(frequency*(2*3.141592)*(offset+i)/audio_samplerate);
        dest[i] = (sample<<16) | (sample & 0xffff);
    }
    
    DSP_FlushDataCache(audioBuffer,size);
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    
    /*Result rc = romfsInit();
    if (rc) printf("romfsInit: %08lX\n", rc);
    else {
        printf("romfs Init Successful!\n");
        printfile("romfs:/folder/file.txt");
        // Test reading a file with non-ASCII characters in the name
        printfile("romfs:/フォルダ/ファイル.txt");
    }*/
    
    //init audio
    audio_init();
    
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
    
    std::cout << "Hello from C++!\n";
    
    
    // Main loop
    while (aptMainLoop()) {
        
        //Get input
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {break;} // break in order to return to hbmenu
        
        //fill audio buffers
        if (waveBuf[fillBlock].status == NDSP_WBUF_DONE) {
            audio_fillbuffer(waveBuf[fillBlock].data_pcm16, stream_offset, waveBuf[fillBlock].nsamples,400);
            ndspChnWaveBufAdd(0, &waveBuf[fillBlock]);
            stream_offset += waveBuf[fillBlock].nsamples;
            fillBlock = !fillBlock;
        }
        
        //Draw colors on bottom screen
        for(unsigned int y=0;y<yresfbb;y++) {
            for(unsigned int x=0;x<yresfbb;x++) {
                fbb[(y*xresfbb+x)*3  ] = x/2+colorOffsetCounter;
                fbb[(y*xresfbb+x)*3+1] = y/2+colorOffsetCounter;
                fbb[(y*xresfbb+x)*3+2] = x/2+y/2;
            }
        }
        /*//Draw colors on top screen
        for(unsigned int y=0;y<yresfbt;y++) {
            for(unsigned int x=0;x<yresfbt;x++) {
                fbt[(y*xresfbt+x)*3  ] = x/2+colorOffsetCounter+127;
                fbt[(y*xresfbt+x)*3+1] = y/2+colorOffsetCounter+127;
                fbt[(y*xresfbt+x)*3+2] = x/2+y/2+127;
            }
        }*/
        colorOffsetCounter++;
        
        //Flush buffers and wait for vblank
        gspWaitForVBlank();
        gfxFlushBuffers();
        gfxSwapBuffers();
    }
    
    //romfsExit();
    gfxExit();
    audio_deinit();
    return 0;
}
