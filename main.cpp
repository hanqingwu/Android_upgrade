#include "include/HttpClient.h"

#include "include/upgrade.h"


#include "unistd.h"

#undef LOG_TAG 
#define LOG_NDDBUG 0
#define LOG_TAG  "upgrade"
#include  <utils/Log.h>


void progress_callback(void *, double download_speed, double remaining_time, double progress_percentage)
{
    //qDebug()<<download_speed<<remaining_time<<progress_percentage;
    int hours = 0, minutes = 0, seconds = 0;

    if (download_speed != 0)
    {
        hours = remaining_time / 3600;
        minutes = (remaining_time - hours * 3600) / 60;
        seconds = remaining_time - hours * 3600 - minutes * 60;
    }

    string unit = "B";
    if (download_speed > 1024 * 1024 * 1024)
    {
        unit = "G";
        download_speed /= 1024 * 1024 * 1024;
    }
    else if (download_speed > 1024 * 1024)
    {
        unit = "M";
        download_speed /= 1024 * 1024;
    }
    else if (download_speed > 1024)
    {
        unit = "kB";
        download_speed /= 1024;
    }

    char speedFormat[15] = { 0 };
    char timeFormat[10] = { 0 };
    char progressFormat[8] = { 0 };

#ifdef _WIN32
    sprintf_s(speedFormat, sizeof(speedFormat), "%.2f%s/s", download_speed, unit.c_str());
    sprintf_s(timeFormat, sizeof(timeFormat), "%02d:%02d:%02d", hours, minutes, seconds);
    sprintf_s(progressFormat, sizeof(progressFormat), "%.2f", progress_percentage);
#else
    sprintf(speedFormat, "%.2f%s/s", download_speed, unit.c_str());
    sprintf(timeFormat, "%02d:%02d:%02d", hours, minutes, seconds);
    sprintf(progressFormat, "%.2f", progress_percentage);
#endif

    //AnyClass *eg = static_cast<AnyClass *>(userdata);
    //eg->func(speedFormat, timeFormat, progressFormat);
    printf("%s %s %s\n", speedFormat, timeFormat, progressFormat);
}

void demo_call_back(int ret)
{
    ALOGI("demo upgrade result call back  %d", ret);


    if (ret == 1)
    {
        ALOGE("download ok");
        upgrade();
    }
    else if (ret== 0)
    {
        ALOGE("demo download error ");
        
//        exit(1);
    }

//    exit(1);
}



int main()
{    

    const char down_url[] = "http://192.168.1.100/test.zip";


    uint32_t time = 0;
    int status, flag;
    int ret;

    ALOGI("start test ");
    while(1)
    {
        ret = upgrade_download((char *)down_url,  -1, demo_call_back);
        ALOGI("upgrade_download ret %d ", ret);

        while(1)
        {
            upgrade_status_get(&status, &flag);
            ALOGI("status %d, flag %d", status, flag );
            sleep(3);
            
//            break;
        }
        upgrade_download_cancel();

        sleep(10);

    }

    while(1);

    //upgrade_file((char*)local_uri);

    return 0;

}
