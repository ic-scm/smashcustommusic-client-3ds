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

//Playback stream mode
bool audio_brstm_isNetworkStreaming = false;

//audio stuff
unsigned int audio_samplerate = 0;
unsigned int audio_samplesperbuf = 0;

bool audio_fillBlock = false;
ndspWaveBuf waveBuf[2];
uint32_t *audioBuffer;

//Playback and decoding thread
Thread brstmThread;
//Stop signal to the playback and decoding thread
bool audio_brstmSTOP = false;

//Network thread data
const char* audio_brstm_netthread_filename = "/3ds/scm_3ds/dl_stream.brstm";
struct audio_brstm_netthread_data_t {
    const char* url;
    bool finished;
    unsigned char error;
    uint32_t bytes_written;
};
audio_brstm_netthread_data_t* audio_brstm_netthread_data;
//Networking thread
Thread brstmNetThread;


//Playback state
unsigned long playback_current_sample=0;
bool audio_brstm_paused = true;


//Functions
unsigned char audio_init();
void audio_deinit();
unsigned char audio_fillbuffer(void *audioBuffer,size_t size);
void audio_mainloop(void* arg);
void audio_brstm_togglepause();
void audio_brstm_seek(signed long samples);
void audio_brstm_seekto(unsigned long targetsample);
void audio_brstm_stop();
unsigned char audio_brstm_play(const char* filename);
size_t audio_brstm_netthread_curlcb(void *ptr, size_t size, size_t nmemb, void *stream);
void audio_brstm_netthread(void* arg);
unsigned char audio_brstm_netstream_play(const char* url);
bool audio_brstm_init();
void audio_brstm_exit();


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

unsigned char audio_fillbuffer(void *audioBuffer,size_t size) {
    uint32_t *dest = (uint32_t*)audioBuffer;
    
    //In network stream mode, check if there is enough data ahead of the play head.
    bool netstream_ok = 1;
    if(audio_brstm_isNetworkStreaming && !audio_brstm_netthread_data->finished) {
        uint32_t required_blocks = ceil((float)(playback_current_sample + size) / audio_brstm_s->blocks_samples);
        uint32_t required_bytes = audio_brstm_s->audio_offset + (audio_brstm_s->blocks_size * audio_brstm_s->num_channels * required_blocks);
        if(required_bytes > audio_brstm_netthread_data->bytes_written) netstream_ok = 0;
    }
    
    int playback_seconds = playback_current_sample / audio_brstm_s->sample_rate;
    std::cout << '\r';
    if(audio_brstm_paused) {std::cout << "Paused ";}
    std::cout << "(" << playback_seconds << "/" << "??:??" << ") ("
    << (audio_brstm_isNetworkStreaming ? (netstream_ok ? "Stream" : "Buffer") : "Normal") << (audio_brstm_isNetworkStreaming ? (audio_brstm_netthread_data->finished ? " done" : "ing..") : "")
    << ") (< >:Seek): \r" << std::flush;
    
    if(!audio_brstm_paused && netstream_ok) {
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

//Playback/decode thread (brstmThread)
void audio_mainloop(void* arg) {
    while(!audio_brstmSTOP) {
        //10ms
        svcSleepThread(10 * 1000000);
        
        //Audio buffer fill
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
        
        //Check network stream status
        if(audio_brstm_isNetworkStreaming && audio_brstm_isopen) {
            if(audio_brstm_netthread_data->finished && audio_brstm_netthread_data->error) {
                printf("\nNetwork stream error, closing file. (%u)\n", audio_brstm_netthread_data->error);
                audio_brstm_stop();
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

//Load a BRSTM from file
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
    audio_brstm_isNetworkStreaming = false;
    if(audio_init()) {
        //NDSP init error
        audio_brstm_stop();
        return 100;
    }
    
    return 0;
}

//Network thread CURL callback
size_t audio_brstm_netthread_curlcb(void *ptr, size_t size, size_t nmemb, void *stream) {
    audio_brstm_netthread_data_t* ntdata = (audio_brstm_netthread_data_t*)audio_brstm_netthread_data;
    
    size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
    
    ntdata->bytes_written += written;
    
    return written;
}

//Network thread function (brstmNetThread)
void audio_brstm_netthread(void* arg) {
    audio_brstm_netthread_data_t* ntdata = (audio_brstm_netthread_data_t*)arg;
    ntdata->finished = 0;
    ntdata->error = 0;
    ntdata->bytes_written = 0;
    
    unsigned char download_res = http_downloadfile(ntdata->url, audio_brstm_netthread_filename, audio_brstm_netthread_curlcb);
    ntdata->url = NULL;
    
    ntdata->finished = 1;
    ntdata->error = download_res;
}

//Load a BRSTM through network streaming
unsigned char audio_brstm_netstream_play(const char* url) {
    printf("ss1\n");
    //Create networking thread
    //Get priority
    int32_t prio;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio++;
    //Set URL
    audio_brstm_netthread_data->url = url;
    //Start thread
    brstmNetThread = threadCreate(audio_brstm_netthread, (void*)audio_brstm_netthread_data, 4096, prio, -2, true);
    if(brstmNetThread == NULL) return 1;
    
    printf("ss2\n");
    //Wait for thread to do something
    while(audio_brstm_netthread_data->bytes_written < 1024) {
        usleep(10000);
        if(audio_brstm_netthread_data->finished) break;
    }
    
    //Check for error
    if(audio_brstm_netthread_data->finished && audio_brstm_netthread_data->error) {
        return 2;
    }
    
    printf("ss3\n");
    //Enough of the file should be downloaded to check base information now.
    if(audio_brstm_isopen) {audio_brstm_stop();}
    audio_brstm_file.open(audio_brstm_netthread_filename, std::ios::in|std::ios::binary);
    if(!audio_brstm_file.is_open()) {
        return 3;
    }
    
    printf("ss4\n");
    //Read BRSTM base information
    audio_brstm_s = new Brstm;
    unsigned char result = brstm_fstream_getBaseInformation(audio_brstm_s, audio_brstm_file, 1);
    if(result>127) {
        audio_brstm_file.close();
        delete audio_brstm_s;
        return result;
    }
    
    printf("ss5\n");
    //Now wait for enough data to be downloaded to read the full file header.
    while(audio_brstm_netthread_data->bytes_written < audio_brstm_s->audio_offset) {
        usleep(10000);
        if(audio_brstm_netthread_data->finished) break;
    }
    
    printf("ss6\n");
    //Read BRSTM file headers
    result = brstm_fstream_read(audio_brstm_s, audio_brstm_file, 1);
    if(result>127) {
        audio_brstm_file.close();
        delete audio_brstm_s;
        return result;
    }
    
    printf("ss7\n");
    //set NDSP sample rate to the BRSTM's sample rate
    audio_samplerate = audio_brstm_s->sample_rate;
    audio_samplesperbuf = AUDIO_OUTPUT_BUFSIZE;
    audio_brstm_isopen = true;
    audio_brstm_paused = false;
    audio_brstm_isNetworkStreaming = true;
    if(audio_init()) {
        //NDSP init error
        audio_brstm_stop();
        return 4;
    }
    
    printf("ss8\n");
    return 0;
}

//Initialize BRSTM playback thread (run this at the beginning of main)
bool audio_brstm_init() {
    //Allocate network thread data
    audio_brstm_netthread_data = (audio_brstm_netthread_data_t*)malloc(sizeof(audio_brstm_netthread_data_t));
    if(audio_brstm_netthread_data == NULL) return 1;
    
    //Initialize main thread
    int32_t prio;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    brstmThread = threadCreate(audio_mainloop, (void*)(0), 4096, prio-1, -2, false);
    if(brstmThread == NULL) return 1;
    
    return 0;
}

//Stop playback and end the thread (run this at the end of main/when exiting the program)
void audio_brstm_exit() {
    audio_brstm_stop();
    audio_brstmSTOP = true;
    threadJoin(brstmThread, U64_MAX);
    threadFree(brstmThread);
    free(audio_brstm_netthread_data);
}
