#include <cstdio> //remove these later
#include <inttypes.h>

#include "curl/curl.h"

#pragma once

Result http_download(const char *url)
{
    Result ret=0;
    httpcContext context;
    char *newurl=NULL;
    u8* framebuf_top;
    u32 statuscode=0;
    u32 contentsize=0, readsize=0, size=0;
    u8 *buf, *lastbuf;
    
    printf("Downloading %s\n",url);
    
    do {
        ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
        printf("return from httpcOpenContext: %" PRId32 "\n",ret);
        
        // This disables SSL cert verification, so https:// will be usable
        ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
        printf("return from httpcSetSSLOpt: %" PRId32 "\n",ret);
        
        // Enable Keep-Alive connections
        ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
        printf("return from httpcSetKeepAlive: %" PRId32 "\n",ret);
        
        // Set a User-Agent header so websites can identify your application
        ret = httpcAddRequestHeaderField(&context, "User-Agent", "httpc-example/1.0.0");
        printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);
        
        // Tell the server we can support Keep-Alive connections.
        // This will delay connection teardown momentarily (typically 5s)
        // in case there is another request made to the same server.
        ret = httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
        printf("return from httpcAddRequestHeaderField: %" PRId32 "\n",ret);
        
        ret = httpcBeginRequest(&context);
        if(ret!=0){
            httpcCloseContext(&context);
            if(newurl!=NULL) free(newurl);
            return ret;
        }
        
        ret = httpcGetResponseStatusCode(&context, &statuscode);
        if(ret!=0){
            httpcCloseContext(&context);
            if(newurl!=NULL) free(newurl);
            return ret;
        }
        
        if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
            if(newurl==NULL) newurl = (char*)malloc(0x1000); // One 4K page for new URL
            if (newurl==NULL){
                httpcCloseContext(&context);
                return -1;
            }
            ret = httpcGetResponseHeader(&context, "Location", newurl, 0x1000);
            url = newurl; // Change pointer to the url that we just learned
            printf("redirecting to url: %s\n",url);
            httpcCloseContext(&context); // Close this context before we try the next
        }
    } while ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308));
    
    if(statuscode!=200){
        printf("URL returned status: %" PRId32 "\n", statuscode);
        httpcCloseContext(&context);
        if(newurl!=NULL) free(newurl);
        return -2;
    }
    
    // This relies on an optional Content-Length header and may be 0
    ret=httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if(ret!=0){
        httpcCloseContext(&context);
        if(newurl!=NULL) free(newurl);
        return ret;
    }
    
    printf("reported size: %" PRId32 "\n",contentsize);
    
    // Start with a single page buffer
    buf = (u8*)malloc(0x1000);
    if(buf==NULL){
        httpcCloseContext(&context);
        if(newurl!=NULL) free(newurl);
        return -1;
    }
    
    do {
        // This download loop resizes the buffer as data is read.
        ret = httpcDownloadData(&context, buf+size, 0x1000, &readsize);
        size += readsize; 
        if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING){
            lastbuf = buf; // Save the old pointer, in case realloc() fails.
            buf = (u8*)realloc(buf, size + 0x1000);
            if(buf==NULL){ 
                httpcCloseContext(&context);
                free(lastbuf);
                if(newurl!=NULL) free(newurl);
                return -1;
            }
        }
    } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);
    
    
    
    if(ret!=0){
        httpcCloseContext(&context);
        if(newurl!=NULL) free(newurl);
        free(buf);
        return -1;
    }
    
    // Resize the buffer back down to our actual final size
    lastbuf = buf;
    buf = (u8*)realloc(buf, size);
    if(buf==NULL){ // realloc() failed.
        httpcCloseContext(&context);
        free(lastbuf);
        if(newurl!=NULL) free(newurl);
        return -1;
    }
    
    printf("downloaded size: %" PRId32 "\n",size);
    
    httpcCloseContext(&context);
    free(buf);
    if (newurl!=NULL) free(newurl);
    
    return 0;
}


//stolen from ctgp7

static size_t file_buffer_pos = 0;
static size_t file_toCommit_size = 0;
static char* g_buffers[2] = { NULL };
static u8 g_index = 0;
static Thread fsCommitThread;
static LightEvent readyToCommit;
static LightEvent waitCommit;
static bool killThread = false;
static bool writeError = false;
#define FILE_ALLOC_SIZE 0x60000

bool filecommit() {
    if (!downfile) return false;
    fseek(downfile, 0, SEEK_END);
    u32 byteswritten = fwrite(g_buffers[!g_index], 1, file_toCommit_size, downfile);
    if (byteswritten != file_toCommit_size) return false;
    file_toCommit_size = 0;
    return true;
}

static void commitToFileThreadFunc(void* args) {
    LightEvent_Signal(&waitCommit);
    while (true) {
        LightEvent_Wait(&readyToCommit);
        LightEvent_Clear(&readyToCommit);
        if (killThread) threadExit(0);
        writeError = !filecommit();
        LightEvent_Signal(&waitCommit);
    }
}

static size_t file_handle_data(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)userdata;
    const size_t bsz = size * nmemb;
    size_t tofill = 0;
    if (writeError) return 0;
    if (!g_buffers[g_index]) {
        
        LightEvent_Init(&waitCommit, RESET_STICKY);
        LightEvent_Init(&readyToCommit, RESET_STICKY);
        
        s32 prio = 0;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        fsCommitThread = threadCreate(commitToFileThreadFunc, NULL, 0x1000, prio - 1, -2, true);
        
        g_buffers[0] = memalign(0x1000, FILE_ALLOC_SIZE);
        g_buffers[1] = memalign(0x1000, FILE_ALLOC_SIZE);
        
        if (!fsCommitThread || !g_buffers[0] || !g_buffers[1]) return 0;
    }
    if (file_buffer_pos + bsz >= FILE_ALLOC_SIZE) {
        tofill = FILE_ALLOC_SIZE - file_buffer_pos;
        memcpy_ctr(g_buffers[g_index] + file_buffer_pos, ptr, tofill);
        
        LightEvent_Wait(&waitCommit);
        LightEvent_Clear(&waitCommit);
        file_toCommit_size = file_buffer_pos + tofill;
        file_buffer_pos = 0;
        svcFlushProcessDataCache(CURRENT_PROCESS_HANDLE, g_buffers[g_index], file_toCommit_size);
        g_index = !g_index;
        LightEvent_Signal(&readyToCommit);
    }
    memcpy_ctr(g_buffers[g_index] + file_buffer_pos, ptr + tofill, bsz - tofill);
    file_buffer_pos += bsz - tofill;
    return bsz;
}

int downloadFile(char* URL, char* filepath, progressbar_t* progbar) {
    
    int retcode = 0;
    
    
    void *socubuf = memalign(0x1000, 0x100000);
    if (!socubuf) {
        sprintf(CURL_lastErrorCode, "Failed to allocate memory.");
        retcode = 1;
        goto exit;
    }
    
    int res = socInit(socubuf, 0x100000);
    if (R_FAILED(res)) {
        sprintf(CURL_lastErrorCode, "socInit returned: 0x%08X", res);
        goto exit;
    }
    
    downfile = fopen_mkdir(filepath, "wb");
    if (!downfile || !progbar) {
        sprintf(CURL_lastErrorCode, "Failed to create file.");
        retcode = 4;
        goto exit;
    }
    
    file_progbar = progbar;
    progbar->isHidden = false;
    progbar->rectangle->amount = 0;
    updatingFile = getFileFromPath(filepath);
    clearTop(false);
    newAppTop(DEFAULT_COLOR, CENTER | BOLD | MEDIUM, updatingVer);
    newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "Downloading Files");
    newAppTop(DEFAULT_COLOR, CENTER | MEDIUM, "%d / %d", fileDownCnt, totFileDownCnt);
    updateUI();
    
    CURL *hnd = curl_easy_init();
    curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, FILE_ALLOC_SIZE);
    curl_easy_setopt(hnd, CURLOPT_URL, URL);
    curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 0L); 
    curl_easy_setopt(hnd, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo 3DS; U; ; en) AppleWebKit/536.30 (KHTML, like Gecko) CTGP-7/1.0 CTGP-7/1.0");
    curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(hnd, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(hnd, CURLOPT_ACCEPT_ENCODING, "gzip");
    curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
    curl_easy_setopt(hnd, CURLOPT_XFERINFOFUNCTION, file_progress_callback);
    curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, file_handle_data);
    curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, CURL_lastErrorCode);
    curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(hnd, CURLOPT_STDERR, stdout);
    
    CURL_lastErrorCode[0] = 0;
    CURLcode cres = curl_easy_perform(hnd);
    curl_easy_cleanup(hnd);
    
    if (cres != CURLE_OK) {
        retcode = cres;
        goto exit;
    }
    
    LightEvent_Wait(&waitCommit);
    LightEvent_Clear(&waitCommit);
    
    file_toCommit_size = file_buffer_pos;
    svcFlushProcessDataCache(CURRENT_PROCESS_HANDLE, g_buffers[g_index], file_toCommit_size);
    g_index = !g_index;
    if (!filecommit()) {
        sprintf(CURL_lastErrorCode, "Couldn't commit to file.");
        retcode = 2;
        goto exit;
    }
    fflush(downfile);
    
    exit:
    if (fsCommitThread) {
        killThread = true;
        LightEvent_Signal(&readyToCommit);
        threadJoin(fsCommitThread, U64_MAX);
        killThread = false;
        fsCommitThread = NULL;
    }
    
    socExit();
    
    if (socubuf) {
        free(socubuf);
    }
    if (downfile) {
        fclose(downfile);
        downfile = NULL;
    }
    if (g_buffers[0]) {
        free(g_buffers[0]);
        g_buffers[0] = NULL;
    }
    if (g_buffers[1]) {
        free(g_buffers[1]);
        g_buffers[1] = NULL;
    }
    g_index = 0;
    file_progbar = NULL;
    file_singlePixel = -1;
    file_buffer_pos = 0;
    file_toCommit_size = 0;
    writeError = false;
    updatingFile = NULL;
    
    return retcode;
}
