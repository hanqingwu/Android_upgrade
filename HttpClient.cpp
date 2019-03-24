#include "include/HttpClient.h"
#include "include/upgrade.h"
#include <sys/stat.h>


#undef LOG_TAG 
#define LOG_NDDBUG 0
#define LOG_TAG  "upgrade"
#include  <utils/Log.h>


#define  DOWNLOAD_LOG_FILE    "/data/upgrade.log"

#define ZLOG(...)   ALOGI("%s",__VA_ARGS__)

HttpClient *HttpClient::instance = NULL;

double HttpClient::downloadFileLength = -1;
curl_off_t HttpClient::resumeByte = -1;
time_t HttpClient::lastTime = 0;

bool HttpClient::stopCurl = false;

HttpClient::HttpClient()
{
}


HttpClient::~HttpClient()
{
}

HttpClient *HttpClient::getInstance()
{
    if (instance == NULL)
	{
		instance = new HttpClient();
		instance->init();
        stopCurl = false;
	}

	return instance;
}

void HttpClient::destroyInstance()
{
    if (instance != NULL)
	{
        stopCurl = true;

        // curl_global_cleanup()    will crash on Qt windows

        delete instance;
        instance = NULL;
	}
}

bool HttpClient::init()
{
    if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
        return false;

    return true;
}

int HttpClient::HttpGetNoBlock(const string requestURL, const string saveTo, void *sender, event_callback cb, int file_size)
{
    string partPath = saveTo;

    CURL *multi_handle = NULL;
    CURL *easy_handle = NULL;

    FILE *fp = NULL;

    int ret = UPGRADE_DOWNLOADING;

    mFileSize = file_size;
    do
    {
        // Get the file size on the server
        downloadFileLength = getDownloadFileLength(requestURL);
        if (downloadFileLength < 0)
        {
            ZLOG("getDownloadFileLength error");
            ret = UPGRADE_DOWNLOAD_FAIL_REMOTE_ACCESS;
            break;
        }

        if (mFileSize != -1 &&  mFileSize != downloadFileLength)
        {
            ALOGE(" File Error, get %d,  http url %f ", mFileSize, downloadFileLength);
            ret = UPGRADE_DOWNLOAD_FAIL_FILE_REMOTE_SIZE;;
            break;
        }

        multi_handle = curl_multi_init();
        if (!multi_handle)
        {
            ZLOG("curl_multi_init error");
            ret = UPGRADE_DOWNLOAD_FAIL_CURL_INIT;
            break;
        }
        easy_handle = curl_easy_init();

        unlink(partPath.c_str());
#ifdef _WIN32
        fopen_s(&fp, partPath.c_str(), "ab+");
#else
		fp = fopen(partPath.c_str(), "wb");
#endif
        if (fp == NULL)
        {
            ZLOG("file open failed");
            ZLOG(partPath.c_str());
            ret = UPGRADE_DOWNLOAD_FAIL_LOCAL_ACCESS;
            break;
        }

        // Set the url
        ret = curl_easy_setopt(easy_handle, CURLOPT_URL, requestURL.c_str());

        // Save data from the server
        ret |= curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, &HttpClient::write_callback);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, fp);

		Progress_User_Data data = { sender, easy_handle, NULL };

        // Get the download progress
        ret |= curl_easy_setopt(easy_handle, CURLOPT_NOPROGRESS, 0L);
        
        ret |= curl_easy_setopt(easy_handle, CURLOPT_XFERINFOFUNCTION, &HttpClient::progress_callback);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_XFERINFODATA, &data);

        // Fail the request if the HTTP code returned is equal to or larger than 400
        ret |= curl_easy_setopt(easy_handle, CURLOPT_FAILONERROR, 1L);

        // The maximum time that allow the connection parse to the server
        ret |= curl_easy_setopt(easy_handle, CURLOPT_CONNECTTIMEOUT, 10L);

        ret |= curl_easy_setopt(easy_handle, CURLOPT_LOW_SPEED_TIME, 60L);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_LOW_SPEED_LIMIT, 30L);

        ret |= curl_easy_setopt(easy_handle, CURLOPT_NOSIGNAL,1);


		resumeByte = getLocalFileLength(partPath);
        if (resumeByte > 0)
        {
            // Set a point to resume transfer
            ret |= curl_easy_setopt(easy_handle, CURLOPT_RESUME_FROM_LARGE, resumeByte);
        }

        if (ret != CURLE_OK)
        {
            ret = UPGRADE_DOWNLOAD_FAIL_FILE_REMOTE_SIZE;
            ZLOG("curl_easy_setopt error");
            break;
        }


//        ret = curl_easy_perform(easy_handle);
        ret = curl_multi_add_handle(multi_handle, easy_handle);      
        ALOGI("curl multl add handle %d\n", ret);
        

        int running_handle_count;
        int time_tick = 0;
        do
        {
            ret =  curl_multi_perform(multi_handle, &running_handle_count);
            if (ret != CURLE_OK)
            {
                ret = UPGRADE_DOWNLOAD_FAIL_NETWORK;
                break;
            }
          
            if (stopCurl)
            {
                //失败处理
                ret = UPGRADE_DOWNLOAD_FAIL_POSITIVE_CANCEL;
                ALOGI("get stop curl \n");
                break;
            }    

            usleep(1);

        }
        while(running_handle_count > 0);

    } while (0);

    ALOGI("http client exit ");
   
    int  seek_get_size = 0;
	if (fp != NULL)
	{
        seek_get_size = ftell(fp);
        fflush(fp);
        fsync(fileno(fp));
		fclose(fp);
		fp = NULL;
	}

    curl_easy_cleanup(easy_handle);
    curl_multi_cleanup(multi_handle);
    easy_handle = NULL;


    //增加枪查文件大小
    if (ret == CURLE_OK)
    {
        ALOGI("seek file size %d, request size %d", seek_get_size, mFileSize);
        if (seek_get_size != mFileSize)
        {
            ret = UPGRADE_DOWNLOAD_FAIL_LOCAL_SIZE;
        }

    }

    char tmpbuf[512];

    sprintf(tmpbuf,"echo \"%d,%d\" > %s", seek_get_size, mFileSize, DOWNLOAD_LOG_FILE);
    system(tmpbuf);

    sprintf(tmpbuf, "md5sum %s >> %s", saveTo.c_str(), DOWNLOAD_LOG_FILE);
    system(tmpbuf);


    if (cb)
       cb(ret);

    return ret;
}



// Return 0 if success, otherwise return error code
int HttpClient::HttpGet(const string requestURL, const string saveTo, void *sender, progress_info_callback cb, long timeout_s)
{
    string partPath = saveTo + ".part";

    CURL *easy_handle = NULL;
    FILE *fp = NULL;

    int ret = HTTP_REQUEST_ERROR;

    do
    {
        // Get the file size on the server
        downloadFileLength = getDownloadFileLength(requestURL);

        if (downloadFileLength < 0)
        {
            ZLOG("getDownloadFileLength error");
            break;
        }

        easy_handle = curl_easy_init();
        if (!easy_handle)
        {
            ZLOG("curl_easy_init error");
            break;
        }

#ifdef _WIN32
        fopen_s(&fp, partPath.c_str(), "ab+");
#else
		fp = fopen(partPath.c_str(), "ab+");
#endif
        if (fp == NULL)
        {
            ZLOG("file open failed");
            ZLOG(partPath.c_str());
            break;
        }

        // Set the url
        ret = curl_easy_setopt(easy_handle, CURLOPT_URL, requestURL.c_str());

        // Save data from the server
        ret |= curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, &HttpClient::write_callback);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, fp);

		Progress_User_Data data = { sender, easy_handle, cb};

        // Get the download progress
        ret |= curl_easy_setopt(easy_handle, CURLOPT_NOPROGRESS, 0L);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_XFERINFOFUNCTION, &HttpClient::progress_callback);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_XFERINFODATA, &data);

        // Fail the request if the HTTP code returned is equal to or larger than 400
        ret |= curl_easy_setopt(easy_handle, CURLOPT_FAILONERROR, 1L);

        // The maximum time that allow the connection parse to the server
        ret |= curl_easy_setopt(easy_handle, CURLOPT_CONNECTTIMEOUT, 10L);

        ret |= curl_easy_setopt(easy_handle, CURLOPT_NOSIGNAL,1);

        ret |= curl_easy_setopt(easy_handle, CURLOPT_TIMEOUT,timeout_s);

		resumeByte = getLocalFileLength(partPath);
        if (resumeByte > 0)
        {
            // Set a point to resume transfer
            ret |= curl_easy_setopt(easy_handle, CURLOPT_RESUME_FROM_LARGE, resumeByte);
        }

        if (ret != CURLE_OK)
        {
			ret = HTTP_REQUEST_ERROR;
            ZLOG("curl_easy_setopt error");
            break;
        }

        ret = curl_easy_perform(easy_handle);

        if (ret != CURLE_OK)
        {
			char s[100] = { 0 };
#ifdef _WIN32
			sprintf_s(s, sizeof(s), "error:%d:%s", ret, curl_easy_strerror(static_cast<CURLcode>(ret)));
#else
			sprintf(s, "error:%d:%s", ret, curl_easy_strerror(static_cast<CURLcode>(ret)));
#endif

            ZLOG(s);
            switch (ret)
            {
                case CURLE_HTTP_RETURNED_ERROR:
                {
                    int code = 0;
                    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &code);

					char s[100] = { 0 };
#ifdef _WIN32
					sprintf_s(s, sizeof(s), "HTTP error code:%d", code);
#else
					sprintf(s, "HTTP error code:%d", code);
#endif
                    ZLOG(s);
                    break;
                }
            }
        }
    } while (0);

	if (fp != NULL)
	{
		fclose(fp);
		fp = NULL;
	}

    curl_easy_cleanup(easy_handle);
    easy_handle = NULL;

    if (ret == CURLE_OK)
    {
        remove(saveTo.c_str());
		rename(partPath.c_str(), saveTo.c_str());
    }

    return ret;
}

size_t HttpClient::write_callback(char *buffer, size_t size, size_t nmemb, void *userdata)
{
	FILE *fp = static_cast<FILE *>(userdata);

	size_t length = fwrite(buffer, size, nmemb, fp);
	if (length != nmemb)
	{
		return length;
	}


	return size * nmemb;
}

int HttpClient::progress_callback(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;

    if(stopCurl)
        return 1;

    time_t now = time(NULL);
	if (now - lastTime < 1)
	{
		return 0;
	}

	lastTime = now;

    Progress_User_Data *data = static_cast<Progress_User_Data *>(userdata);
	CURL *easy_handle = data->handle;

	// Defaults to bytes/second
	double speed = 0;
	curl_easy_getinfo(easy_handle, CURLINFO_SPEED_DOWNLOAD, &speed);

	// Time remaining
	double leftTime = 0;

	// Progress percentage
	double progress = 0;

	if (dltotal != 0 && speed != 0)
	{
		progress = (dlnow + resumeByte) / downloadFileLength * 100;
		leftTime = (downloadFileLength - dlnow - resumeByte) / speed;
//		ALOGI("\t%.2f%%\tRemaing time:%s\n", progress, timeFormat);
        ALOGI("progress %f leftTime %f", progress, leftTime);
	}

    if (data->cb != NULL)
		data->cb(data->sender, speed, leftTime, progress);

	return 0;
}

// Get the local file size, return -1 if failed
p_off_t HttpClient::getLocalFileLength(string path)
{
	p_off_t ret;
	struct stat fileStat;

	ret = stat(path.c_str(), &fileStat);
	if (ret == 0)
	{
		return fileStat.st_size;
	}

	return ret;
}

size_t nousecb(char *buffer, size_t x, size_t y, void *userdata)
{
	(void)buffer;
	(void)userdata;
	return x * y;
}

// Get the file size on the server
double HttpClient::getDownloadFileLength(string url)
{
    CURL *easy_handle = NULL;
	int ret = CURLE_OK;
	double size = -1;

	do
	{
		easy_handle = curl_easy_init();
		if (!easy_handle)
		{
            ZLOG("curl_easy_init error");
			break;
		}

		// Only get the header data
		ret = curl_easy_setopt(easy_handle, CURLOPT_URL, url.c_str());
		ret |= curl_easy_setopt(easy_handle, CURLOPT_HEADER, 1L);
		ret |= curl_easy_setopt(easy_handle, CURLOPT_NOBODY, 1L);
        ret |= curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, nousecb);	// libcurl_a.lib will return error code 23 without this sentence on windows

		if (ret != CURLE_OK)
		{
            ZLOG("curl_easy_setopt error");
			break;
		}

		ret = curl_easy_perform(easy_handle);
		if (ret != CURLE_OK)
		{
			char s[100] = {0};
#ifdef _WIN32
			sprintf_s(s, sizeof(s), "error:%d:%s", ret, curl_easy_strerror(static_cast<CURLcode>(ret)));
#else
			sprintf(s, "error:%d:%s", ret, curl_easy_strerror(static_cast<CURLcode>(ret)));
#endif
            ZLOG(s);
			break;
		}

		// size = -1 if no Content-Length return or Content-Length=0
		ret = curl_easy_getinfo(easy_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &size);
		if (ret != CURLE_OK)
		{
            ZLOG("curl_easy_getinfo error");
			break;
		}

	} while (0);

	curl_easy_cleanup(easy_handle);
	return size;
}
