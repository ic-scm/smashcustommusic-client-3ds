//3DS Smash Custom Music Client
//Copyright (C) 2020 Extrasklep

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
unsigned char* brstmfilememblock;
bool brstm_isopen = false;

//audio stuff
unsigned int audio_samplerate = 0;
unsigned int audio_samplesperbuf = 0;

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
    waveBuf[0].status = NDSP_WBUF_DONE;
    waveBuf[1].status = NDSP_WBUF_DONE;
}

void audio_deinit() {
    ndspExit();
    memset(audioBuffer,0,audio_samplesperbuf*sizeof(u32)*2);
    linearFree(audioBuffer);
    audio_samplerate = 0;
    audio_samplesperbuf = 0;
    fillBlock = false;
    memset(waveBuf,0,sizeof(waveBuf));
}

unsigned long playback_current_sample=0;
bool paused = true;

unsigned char audio_fillbuffer(void *audioBuffer,size_t size) {
    int16_t *dest = (int16_t*)audioBuffer;
    
    if(!paused) {
        brstm_getbuffer(brstmfilememblock,playback_current_sample,audio_samplesperbuf,true);
        unsigned int ch1id = 0;
        unsigned int ch2id = HEAD3_num_channels > 1 ? 1 : 0;
        int ioffset=0;
        
        for(unsigned int i=0; i<audio_samplesperbuf; i++) {
            int16_t sample1 = PCM_buffer[ch1id][i];
            int16_t sample2 = PCM_buffer[ch2id][i];
            dest[i*2]   = sample1;
            dest[i*2+1] = sample2;
            playback_current_sample++;
            
            //loop/end
            if(playback_current_sample>HEAD1_total_samples) {
                if(HEAD1_loop) {
                    playback_current_sample=HEAD1_loop_start;
                    //refill buffer
                    brstm_getbuffer(brstmfilememblock,playback_current_sample,audio_samplesperbuf,true);
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

void audio_mainloop() {
    //fill audio buffers
    if (waveBuf[fillBlock].status == NDSP_WBUF_DONE && brstm_isopen) {
        audio_fillbuffer(waveBuf[fillBlock].data_pcm16, waveBuf[fillBlock].nsamples);
        ndspChnWaveBufAdd(0, &waveBuf[fillBlock]);
        fillBlock = !fillBlock;
    }    
}

void brstm_togglepause() {
    paused=!paused;
}

void brstm_seek(long samples) {
    long targetsample = playback_current_sample;
    targetsample += samples;
    if(targetsample>HEAD1_total_samples) {targetsample=HEAD1_total_samples;}
    if(targetsample<0) {targetsample=0;}
    playback_current_sample = targetsample;
}

void brstm_seekto(long targetsample) {
    if(targetsample>HEAD1_total_samples) {targetsample=HEAD1_total_samples;}
    if(targetsample<0) {targetsample=0;}
    playback_current_sample = targetsample;
}

void stopBrstm() {
    audio_deinit();
    brstm_isopen = false;
    paused = true;
    playback_current_sample = 0;
    delete[] brstmfilememblock;
    brstm_close();
}

unsigned char playBrstm(char* filename) {
    std::cout << "Reading file " << filename << "...\n";
    std::streampos fsize;
    std::ifstream file (filename, std::ios::in|std::ios::binary|std::ios::ate);
    if (file.is_open()) {
        fsize = file.tellg();
        if(brstm_isopen) {stopBrstm();}
        brstmfilememblock = new unsigned char [fsize];
        file.seekg(0, std::ios::beg);
        file.read((char*)brstmfilememblock, fsize);
        file.close();
        
        std::cout << "Read " << fsize << " bytes\n";
        
        //Read the BRSTM file headers
        unsigned char result=readBrstm(brstmfilememblock,1,false);
        if(result>127) {
            std::cout << "Error " << (int)result << "\n";
            delete[] brstmfilememblock;
            return result;
        }
        
        audio_samplerate = HEAD1_sample_rate;
        brstm_isopen = true;
        paused = true;
        audio_init();
        
        return 0;
    } else {
        std::cout << "Unable to open file\n";
        return 1;
    }
    return 1;
}
