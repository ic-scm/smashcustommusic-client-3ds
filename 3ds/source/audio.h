//3DS BRSTM and SmashCustomMusic experiments
//Copyright (C) 2020 I.C.
#include <new>
#pragma once

#include "libopenrevolution/brstm.h"

#define AUDIO_OUTPUT_BUFSIZE 8192

//BRSTM file data
Brstm* audio_brstm_s = nullptr;
std::ifstream audio_brstm_file;
bool audio_brstm_isopen = false;

//audio stuff
unsigned int audio_samplerate = 0;
unsigned int audio_samplesperbuf = 0;

bool audio_fillBlock = false;
ndspWaveBuf waveBuf[2];
uint32_t *audioBuffer;

//Initialize NDSP
unsigned char audio_init() {
    audio_fillBlock=false;
    
    //3 buffers and only using 1 and 2... I have no idea what the devkitpro people broke again but the first buffer would have always caused popping noises.
    //TODO
    audioBuffer = (uint32_t*)linearAlloc(audio_samplesperbuf*sizeof(uint32_t)*3);
    
    if(ndspInit()) {return 1;}
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, audio_samplerate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    
    memset(&waveBuf[0], 0, sizeof(ndspWaveBuf));
    memset(&waveBuf[1], 0, sizeof(ndspWaveBuf));
    
    waveBuf[0].data_vaddr = &audioBuffer[audio_samplesperbuf];
    waveBuf[0].nsamples = audio_samplesperbuf;
    waveBuf[1].data_vaddr = &audioBuffer[audio_samplesperbuf*2];
    waveBuf[1].nsamples = audio_samplesperbuf;
    waveBuf[0].status = NDSP_WBUF_DONE;
    waveBuf[1].status = NDSP_WBUF_DONE;
    return 0;
}

//Deinitialize NDSP
void audio_deinit() {
    ndspExit();
    memset(audioBuffer,0,audio_samplesperbuf*sizeof(uint32_t)*3);
    linearFree(audioBuffer);
    audio_samplerate = 0;
    audio_samplesperbuf = 0;
    audio_fillBlock = false;
}

unsigned long playback_current_sample=0;
bool audio_brstm_paused = true;

unsigned char audio_fillbuffer(void *audioBuffer,size_t size) {
    uint32_t *dest = (uint32_t*)audioBuffer;
    
    int playback_seconds = playback_current_sample / audio_brstm_s->sample_rate;
    std::cout << '\r';
    if(audio_brstm_paused) {std::cout << "Paused ";}
    std::cout << "(" << playback_seconds << "/" << "??:??" << ") (< >:Seek):           \r" << std::flush;
    
    if(!audio_brstm_paused) {
        brstm_fstream_getbuffer(audio_brstm_s,audio_brstm_file,playback_current_sample,
                        //Avoid reading garbage outside the file
                        audio_brstm_s->total_samples-playback_current_sample < size ? audio_brstm_s->total_samples-playback_current_sample : size
                        );
        
        unsigned char ch1id = audio_brstm_s->track_lchannel_id [0];
        unsigned char ch2id = audio_brstm_s->track_num_channels[0] == 2 ? audio_brstm_s->track_rchannel_id[0] : ch1id;
        signed int ioffset=0;
        
        for(unsigned int i=0; i<size; i++) {
            dest[i] = (audio_brstm_s->PCM_buffer[ch2id][(i+ioffset)] << 16) | (audio_brstm_s->PCM_buffer[ch1id][(i+ioffset)] & 0xffff);
            playback_current_sample++;
            
            //loop/end
            if(playback_current_sample >= audio_brstm_s->total_samples) {
                if(audio_brstm_s->loop_flag) {
                    playback_current_sample = audio_brstm_s->loop_start;
                    //refill buffer
                    brstm_fstream_getbuffer(audio_brstm_s, audio_brstm_file, playback_current_sample, size);
                    ioffset = 0-(i+1);
                } else {return 1;}
            }
        }
    } else {
        for(unsigned int i=0; i<size; i++) {
            dest[i] = 0;
        }
    }
    
    DSP_FlushDataCache(audioBuffer,size);
    return 0;
}

//stop signal to the thread
bool audio_brstmSTOP = false;

//Playback/decode thread
void audio_mainloop(void* arg) {
    while(!audio_brstmSTOP) {
        //10ms
        svcSleepThread(10 * 1000000);
        //fill audio buffers
        if (waveBuf[audio_fillBlock].status == NDSP_WBUF_DONE && audio_brstm_isopen) {
            uint8_t fillbuffer_res = audio_fillbuffer(waveBuf[audio_fillBlock].data_pcm16, waveBuf[audio_fillBlock].nsamples);
            ndspChnWaveBufAdd(0, &waveBuf[audio_fillBlock]);
            audio_fillBlock = !audio_fillBlock;
            
            if(fillbuffer_res) {
                //end of file reached. stop playing
                audio_brstm_paused = true;
                playback_current_sample = 0;
            }
            
            //Check system headphone status
            bool headphone_status = 0;
            DSP_GetHeadphoneStatus(&headphone_status);
            //Disable full sleep mode if BRSTM is playing and headphones are plugged in
            if(!audio_brstm_paused && headphone_status) {
                aptSetSleepAllowed(0);
            } else {
                aptSetSleepAllowed(1);
            }
        }
    }
}

//Pause/resume playback
void audio_brstm_togglepause() {
    audio_brstm_paused=!audio_brstm_paused;
}

//Seek (current sample += arg samples)
void audio_brstm_seek(signed long samples) {
    signed long targetsample = playback_current_sample;
    targetsample += samples;
    if(targetsample>(signed long)audio_brstm_s->total_samples) {targetsample=audio_brstm_s->total_samples;}
    if(targetsample<0) {targetsample=0;}
    playback_current_sample = targetsample;
}

//Seek to (current sample = arg samples)
void audio_brstm_seekto(unsigned long targetsample) {
    if(targetsample>audio_brstm_s->total_samples) {targetsample=audio_brstm_s->total_samples;}
    playback_current_sample = targetsample;
}

//Stop playback and unload the BRSTM
void audio_brstm_stop() {
    audio_brstm_paused = true;
    playback_current_sample = 0;
    //don't try to close the brstm if it's not open, it will cause a segfault!
    if(audio_brstm_isopen) {
        audio_deinit();
        audio_brstm_file.close();
        brstm_close(audio_brstm_s);
        delete audio_brstm_s;
        audio_brstm_isopen = false;
    }
}

//Load a BRSTM
unsigned char audio_brstm_play(const char* filename) {
    if(audio_brstm_isopen) {audio_brstm_stop();}
    audio_brstm_file.open(filename, std::ios::in|std::ios::binary);
    if(!audio_brstm_file.is_open()) {
        return 1;
    }
    //Read BRSTM file headers
    audio_brstm_s = new Brstm;
    unsigned char result = brstm_fstream_read(audio_brstm_s,audio_brstm_file,1);
    if(result>127) {
        audio_brstm_file.close();
        delete audio_brstm_s;
        return result;
    }
    
    //set NDSP sample rate to the BRSTM's sample rate
    audio_samplerate = audio_brstm_s->sample_rate;
    audio_samplesperbuf = AUDIO_OUTPUT_BUFSIZE;
    audio_brstm_isopen = true;
    audio_brstm_paused = true;
    if(audio_init()) {
        //NDSP init error
        audio_brstm_stop();
        return 100;
    }
    
    return 0;
}

//Load a BRSTM through network streaming
unsigned char audio_brstm_netstream_play(const char* url) {
    
}

//Initialize BRSTM playback thread (run this at the beginning of main)
Thread brstmThread;
void audio_brstm_init() {
    int32_t prio;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    brstmThread = threadCreate(audio_mainloop, (void*)(0), 4096, prio-1, -2, false);
}

//Stop playback and end the thread (run this at the end of main/when exiting the program)
void audio_brstm_exit() {
    audio_brstm_stop();
    audio_brstmSTOP = true;
    threadJoin(brstmThread, U64_MAX);
    threadFree(brstmThread);
}
