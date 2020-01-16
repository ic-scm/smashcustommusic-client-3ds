//3DS audio and graphics example made by extrasklep copyright license bla bla bla
#include <iostream>
#include <fstream>
#include <cstring>
#include <math.h>
#include <3ds.h>

//brstm stuff
unsigned int  HEAD1_codec; //char
unsigned int  HEAD1_loop;  //char
unsigned int  HEAD1_num_channels; //char
unsigned int  HEAD1_sample_rate;
unsigned long HEAD1_loop_start;
unsigned long HEAD1_total_samples;
unsigned long HEAD1_ADPCM_offset;
unsigned long HEAD1_total_blocks;
unsigned long HEAD1_blocks_size;
unsigned long HEAD1_blocks_samples;
unsigned long HEAD1_final_block_size;
unsigned long HEAD1_final_block_samples;
unsigned long HEAD1_final_block_size_p;
unsigned long HEAD1_samples_per_ADPC;
unsigned long HEAD1_bytes_per_ADPC;

unsigned int  HEAD2_num_tracks;
unsigned int  HEAD2_track_type;

unsigned int  HEAD2_track_num_channels[8] = {0,0,0,0,0,0,0,0};
unsigned int  HEAD2_track_lchannel_id [8] = {0,0,0,0,0,0,0,0};
unsigned int  HEAD2_track_rchannel_id [8] = {0,0,0,0,0,0,0,0};
//type 1 only
unsigned int  HEAD2_track_volume      [8] = {0,0,0,0,0,0,0,0};
unsigned int  HEAD2_track_panning     [8] = {0,0,0,0,0,0,0,0};
//HEAD3
unsigned int  HEAD3_num_channels;

int16_t* PCM_samples[16];
int16_t* PCM_buffer[16];

unsigned long written_samples=0;
#include "brstm.h" //must be included after this stuff

unsigned char* memblock;

//audio stuff
void audio_fillbuffer(void *audioBuffer,size_t offset,size_t size,int frequency);

unsigned int audio_samplerate = 0;
unsigned int audio_samplesperbuf = 0;
//unsigned int audio_bytespersample = 4;

bool fillBlock = false;
ndspWaveBuf waveBuf[2];
u32 *audioBuffer;

void audio_init() {
    fillBlock=false;
    audio_samplesperbuf = (audio_samplerate / 15);
    
    audioBuffer = (u32*)linearAlloc(audio_samplesperbuf*sizeof(u32)*2);
    
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, audio_samplerate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    
    memset(waveBuf,0,sizeof(waveBuf));
    waveBuf[0].data_vaddr = &audioBuffer[0];
    waveBuf[0].nsamples = audio_samplesperbuf;
    waveBuf[1].data_vaddr = &audioBuffer[audio_samplesperbuf];
    waveBuf[1].nsamples = audio_samplesperbuf;
    
    ndspChnWaveBufAdd(0, &waveBuf[0]);
    ndspChnWaveBufAdd(0, &waveBuf[1]);
}

void audio_deinit() {
    ndspExit();
    linearFree(audioBuffer);
}

long playback_current_sample=0;

void audio_fillbuffer(void *audioBuffer,size_t size) {
    int16_t *dest = (int16_t*)audioBuffer;
    
    brstm_getbuffer(memblock,playback_current_sample,audio_samplesperbuf*2,true);
    
    unsigned int ch2id = HEAD3_num_channels > 1 ? 1 : 0;
    
    for(unsigned int i=0; i<audio_samplesperbuf; i++) {
        int16_t sample = PCM_buffer[0][i];
        int16_t sample2 = PCM_buffer[ch2id][i];
        dest[i*2] = /*(sample<<16) | (sample & 0xffff);*/ sample;
        dest[i*2+1] = /*(sample<<16) | (sample & 0xffff);*/ sample2;
        playback_current_sample++;
    }
    /*for(unsigned int i=0; i<audio_samplesperbuf; i++) {
        int16_t sample = PCM_buffer[ch2id][i];
        dest[i] = (sample<<16) | (sample & 0xffff);
    }*/
    
    DSP_FlushDataCache(audioBuffer,size);
}

char* swkb_buf;
char* getSwkbText() {
    delete[] swkb_buf;
    swkb_buf = new char[255];
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 255);
    swkbdSetHintText(&swkbd, "Enter brstm filename");
    swkbdInputText(&swkbd, swkb_buf, 255);
    return swkb_buf;
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
    
    std::cout << "Hello from C++!\nPress A to play a brstm\n";
    
    
    // Main loop
    while (aptMainLoop()) {
        
        //Get input
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {break;} // break in order to return to hbmenu
        
        if (kDown & KEY_A) {
            char* filename = getSwkbText();
            std::cout << "Reading file " << filename << "...\n";
            std::streampos fsize;
            std::ifstream file (filename, std::ios::in|std::ios::binary|std::ios::ate);
            if (file.is_open()) {
                fsize = file.tellg();
                delete[] memblock;
                memblock = new unsigned char [fsize];
                file.seekg (0, std::ios::beg);
                file.read ((char*)memblock, fsize);
                std::cout << "Read " << fsize << " bytes\n";
                
                //read brstm
                unsigned char result=readBrstm(memblock,1,false);
                if(result>127) {
                    std::cout << "Error " << (int)result << "\n";
                    goto brstmread_error;
                }
                
                audio_samplerate = HEAD1_sample_rate;
                audio_init();
                
                goto brstmread_success;
            } else {std::cout << "Unable to open file\n";}
            brstmread_error:;
            delete[] memblock;
        }
        brstmread_success:;
        
        //fill audio buffers
        if (waveBuf[fillBlock].status == NDSP_WBUF_DONE) {
            audio_fillbuffer(waveBuf[fillBlock].data_pcm16, waveBuf[fillBlock].nsamples);
            ndspChnWaveBufAdd(0, &waveBuf[fillBlock]);
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
        colorOffsetCounter++;
        
        //Flush buffers and wait for vblank
        gspWaitForVBlank();
        gfxFlushBuffers();
        gfxSwapBuffers();
    }
    
    //romfsExit();
    gfxExit();
    return 0;
}
