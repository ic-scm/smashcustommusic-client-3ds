#include <cstdio>
#include "curl/curl.h"
#pragma once

CURL *curl_handle;

int http_init() {
    return curl_global_init(CURL_GLOBAL_ALL);
}

void http_exit() {
    curl_global_cleanup();
}

//curl write data function
uint32_t http_totalwritten = 0;
bool     http_contentlength_checked = 0;
uint32_t http_contentlength = 0;
size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream) {
    size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
    http_totalwritten += written;
    if(http_contentlength == 0 && http_contentlength_checked == 0) {
        curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &http_contentlength);
        http_contentlength_checked = 1;
    }
    
    //temporary
    std::cout << "Download " << http_totalwritten << "/" << http_contentlength << "\r" << std::flush;
    
    return written;
}

unsigned char http_downloadfile(const char* url, const char* filename, size_t (*cb)(void*, size_t, size_t, void*)) {
    CURLcode cres;
    FILE *file;
    
    curl_handle = curl_easy_init(); //init the curl session
    curl_easy_setopt(curl_handle, CURLOPT_URL, url); //set URL to get here
    //curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L); //Switch on full protocol/debug output while testing
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L); //disable progress meter, set to 0L to enable it
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, cb); //send all data to this function
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo 3DS; U; ; en) SCMClient3DS/0.1");
    curl_easy_setopt(curl_handle, CURLOPT_STDERR, stdout);
    
    //Open file
    file = fopen(filename, "wb");
    if(file) {
        /* write the page body to this file handle */ 
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, file);
        
        /* get it! */ 
        cres = curl_easy_perform(curl_handle);
        
        /* close the header file */ 
        fclose(file);
        http_totalwritten = 0;
        http_contentlength_checked = 0;
        http_contentlength = 0;
    } else {
        //Unable to open file
        std::cout << "Unable to open file in http_downloadfile\n"; //remove later
        return 255;
    }
    
    curl_easy_cleanup(curl_handle);
    
    if (cres != CURLE_OK) {
        return 128;
    }
    
    return 0;
}

unsigned char http_downloadfile(const char* url, const char* filename) {
    return http_downloadfile(url, filename, write_data);
}
