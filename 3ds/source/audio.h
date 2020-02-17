//3DS Smash Custom Music Client
//Copyright (C) 2020 Extrasklep

#include <new>
#pragma once

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

//BRSTM file data
std::ifstream audio_brstm_file;
bool audio_brstm_isopen = false;

//audio stuff
unsigned int audio_samplerate = 0;
unsigned int audio_samplesperbuf = 0;

bool audio_fillBlock = false;
ndspWaveBuf waveBuf[2];
u32 *audioBuffer;

//Initialize NDSP
unsigned char audio_init() {
    audio_fillBlock=false;
    audio_samplesperbuf = (audio_samplerate / 15);
    
    audioBuffer = (u32*)linearAlloc(audio_samplesperbuf*sizeof(u32)*2);
    
    if(ndspInit()) {return 1;}
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, audio_samplerate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    
    memset(waveBuf,0,sizeof(waveBuf));
    waveBuf[0].data_vaddr = &audioBuffer[0];
    waveBuf[0].nsamples = audio_samplesperbuf;
    waveBuf[1].data_vaddr = &audioBuffer[audio_samplesperbuf];
    waveBuf[1].nsamples = audio_samplesperbuf;
    waveBuf[0].status = NDSP_WBUF_DONE;
    waveBuf[1].status = NDSP_WBUF_DONE;
    return 0;
}

//Deinitialize NDSP
void audio_deinit() {
    ndspExit();
    memset(audioBuffer,0,audio_samplesperbuf*sizeof(u32)*2);
    linearFree(audioBuffer);
    audio_samplerate = 0;
    audio_samplesperbuf = 0;
    audio_fillBlock = false;
    memset(waveBuf,0,sizeof(waveBuf));
}

unsigned long playback_current_sample=0;
bool audio_brstm_paused = true;

unsigned char audio_fillbuffer(void *audioBuffer,size_t size) {
    int16_t *dest = (int16_t*)audioBuffer;
    
    int playback_seconds=playback_current_sample/HEAD1_sample_rate;
    std::cout << '\r';
    if(audio_brstm_paused) {std::cout << "Paused ";}
    std::cout << "(" << playback_seconds << "/" << "??:??" << ") (< >:Seek):           \r";
    
    if(!audio_brstm_paused) {
        brstm_fstream_getbuffer(audio_brstm_file,playback_current_sample,
                        //Avoid reading garbage outside the file
                        HEAD1_total_samples-playback_current_sample < audio_samplesperbuf ? HEAD1_total_samples-playback_current_sample : audio_samplesperbuf
                        );
        unsigned int ch1id = 0;
        unsigned int ch2id = HEAD3_num_channels > 1 ? 1 : 0;
        signed int ioffset=0;
        
        for(unsigned int i=0; i<audio_samplesperbuf; i++) {
            int16_t sample1 = PCM_buffer[ch1id][i+ioffset];
            int16_t sample2 = PCM_buffer[ch2id][i+ioffset];
            dest[i*2]   = sample1;
            dest[i*2+1] = sample2;
            playback_current_sample++;
            
            //loop/end
            if(playback_current_sample > HEAD1_total_samples) {
                if(HEAD1_loop) {
                    playback_current_sample=HEAD1_loop_start;
                    //refill buffer
                    brstm_fstream_getbuffer(audio_brstm_file,playback_current_sample,audio_samplesperbuf);
                    ioffset-=i;
                } else {return 1;}
            }
        }
    } else {
        for(unsigned int i=0; i<audio_samplesperbuf*2; i++) {
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
            if(audio_fillbuffer(waveBuf[audio_fillBlock].data_pcm16, waveBuf[audio_fillBlock].nsamples)) {
                //end of file reached. stop playing
                audio_brstm_paused = true;
                playback_current_sample = 0;
            }
            ndspChnWaveBufAdd(0, &waveBuf[audio_fillBlock]);
            audio_fillBlock = !audio_fillBlock;
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
    if(targetsample>(signed long)HEAD1_total_samples) {targetsample=HEAD1_total_samples;}
    if(targetsample<0) {targetsample=0;}
    playback_current_sample = targetsample;
}

//Seek to (current sample = arg samples)
void audio_brstm_seekto(signed long targetsample) {
    if(targetsample>(signed long)HEAD1_total_samples) {targetsample=HEAD1_total_samples;}
    if(targetsample<0) {targetsample=0;}
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
        brstm_close();
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
    unsigned char result = brstm_fstream_read(audio_brstm_file,1);
    if(result>127) {
        audio_brstm_file.close();
        return result;
    }
    
    //set NDSP sample rate to the BRSTM's sample rate
    audio_samplerate = HEAD1_sample_rate;
    audio_brstm_isopen = true;
    audio_brstm_paused = true;
    if(audio_init()) {
        //NDSP init error
        audio_brstm_stop();
        return 100;
    }
    
    return 0;
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
