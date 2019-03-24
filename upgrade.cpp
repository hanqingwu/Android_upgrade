/*************************************************************************
	> File Name: upgrade.cpp
	> Author: 
	> Mail: 
	> Created Time: 2018年06月08日 星期五 17时22分42秒
 ************************************************************************/

#include<iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cutils/properties.h>
#include <arpa/inet.h>

#include <zlib.h>
#include <dirent.h>

#include "include/upgrade.h"

#include <pthread.h>
#include "include/HttpClient.h"

#include <sys/prctl.h>


#undef LOG_TAG 
#define LOG_NDDBUG 0
#define LOG_TAG  "upgrade"
#include  <utils/Log.h>



static int update_result = 0;

#define UPDATE_FLAG_FILE    "/cache/recovery/last_flag"

#define UNCRYPT_PACKAGE_FILE "/cache/recovery/uncrypt_file" 
#define BLOCK_MAP_FILE      "/cache/recovery/block.map"

#define UNIX_DOMAIN   "/dev/socket/uncrypt"
#define USB_STORAGE_PATH   "/mnt/media_rw/"

#define COMMAND_FLAG_SUCCESS "success"
#define COMMAND_FLAG_UPDATING "updating"


static int  WriteFlagCommand(char *path)
{
    int result = 0;
    int fd = 0;        
    int bytes_write= 0;
    char update_prefix[]= "updating$path=";

    unlink(UPDATE_FLAG_FILE);

    if ( ( fd = open(UPDATE_FLAG_FILE, O_RDWR|O_CREAT)) < 0  )
    {
        ALOGE("Fail to open file '%s', error : '%s'.", UPDATE_FLAG_FILE, strerror(errno) );
        result = fd;
        goto EXIT;
            
    }
        

    if ( ( bytes_write =  write(fd, update_prefix, strlen(update_prefix) ) ) < 0  )
    {
        ALOGE("Fail to write  file '%s', bytes write : '%d', error : '%s'.", UPDATE_FLAG_FILE, bytes_write, strerror(errno) );
        result = errno;
        goto EXIT;
    }

    if ( ( bytes_write =  write(fd, path, strlen(path) ) ) < 0  )
    {
        ALOGE("Fail to write  file '%s', bytes write : '%d', error : '%s'.", UPDATE_FLAG_FILE, bytes_write, strerror(errno) );
        result = errno;
        goto EXIT;
    }
EXIT:
    if ( fd > 0  )
    {
       close(fd);
    }

    return result;

}

static bool uncrypt(char *)
{
    int socket_fd;
    struct sockaddr_un srv_addr;
    char buf[1024];
    int ret = 0; 

    
    property_set("ctl.start", "uncrypt");
   
    socket_fd = socket(PF_UNIX,SOCK_STREAM,0);
    if (socket_fd < 0)
    {
        ALOGE("create local socket error!");
        return false;
    }

    srv_addr.sun_family = AF_UNIX;
    strcpy(srv_addr.sun_path, UNIX_DOMAIN);

    for (int i = 0; i < 30; i++)
    {
        ret = connect(socket_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    
        if (ret >= 0)
            break;

        
        sleep(1);
       
    }

    if (ret < 0)
    {  
        ALOGE("connect server fail ,errno %d", errno);
        close(socket_fd);
        return false;
    }

    ALOGD("connect ok");

    int status;
    int length = 0;
    int last_status = -1;
    
    ret = false;

    while(true)
    {
        length = read(socket_fd, buf, 4);
        status = ntohl(*(uint32_t *)buf);
        if (status == last_status )
        {
            usleep(10000);
            continue;
        }

        last_status = status;
        ALOGD("read status %d", status);

        if (status == 100)
        {
            status = 0;
            ret = true;
            write(socket_fd, &status, 4);
            break;
        }

        usleep(10000);

    }

    close(socket_fd);

    return ret;
}




static bool setupOrClearBcb(bool isSetup, char *command)
{
    int socket_fd;
    struct sockaddr_un srv_addr;
    char buf[1024];
    int ret = 0; 

    if (isSetup)
    {
        property_set("ctl.start", "setup-bcb");
    }
    else
    {
        property_set("ctl.start", "clear-bcb");
    }


    socket_fd = socket(PF_UNIX,SOCK_STREAM,0);
    if (socket_fd < 0)
    {
        ALOGE("create local socket error!");
        return false;
    }

    srv_addr.sun_family = AF_UNIX;
    strcpy(srv_addr.sun_path, UNIX_DOMAIN);

    for (int i = 0; i < 30; i++)
    {
        ret = connect(socket_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    
        if (ret >= 0)
            break;

        
        sleep(1);
       
    }

    if (ret < 0)
    {  
        ALOGE("connect server fail ,errno %d", errno);
        close(socket_fd);
        return false;
    }


    if (isSetup)
    {
        int command_length = strlen(command);
        memset(buf, 0, sizeof(1024));    
        int net_len = htonl(command_length);

        memcpy(buf,&net_len,4);
        strcpy(buf + 4, command);

        write(socket_fd, buf, command_length + 4);
    }

    int status;
    int length = 0;
    length  =  read(socket_fd, buf, 4);

    status = ntohl(*(uint32_t *)buf);
    
    ALOGD("read status %d", status);
    
    length = 0;
    memcpy(buf,&length,4);
    write(socket_fd,buf, 4);

    close(socket_fd);

    if (status == 100)
        return true;
    else
        return false;
}


#define  ZIP_FILE_BUF_SIZE      1024
#define  ZIP_FILE_NAME_SIZE     512

int getdatafromzip(char *zipifie, char* name,  char *value, size_t  value_len)
{
    char filename_inzip[ZIP_FILE_NAME_SIZE];
    unz_file_info file_info;
    char *buf = NULL;
    int ret = -1;
    int ret_value = -1;


    unzFile uf = unzOpen64(zipifie);
    if (uf == NULL)
    {
        printf("can not open file %s not fond, errno %s!\n",zipifie, strerror(errno));
        return -1;
    }

    
    ret = unzLocateFile(uf, "META-INF/com/android/metadata",0);
    if (ret != 0)
    {
        printf(" canot not locate build.prop ret %d\n", ret);
        goto exit;
    }

    if (unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0))
    {
        printf("get file info errno %d", errno);
        goto exit;
    }
    

    buf = (char *)malloc(ZIP_FILE_BUF_SIZE);
    if (buf == NULL)
    {
        printf("malloc error !\n");
        goto exit;
    }


    ret = unzOpenCurrentFilePassword(uf, NULL);
    if (ret != UNZ_OK)
    {
        printf(" open fith passord fail \n");
        goto exit;
    }

//    printf("extracting ... \n");
    do {
        memset(buf, 0, ZIP_FILE_BUF_SIZE);

        ret = unzReadCurrentFile(uf, buf, ZIP_FILE_BUF_SIZE);
        if (ret  < 0 )
        {
            printf("read erro %d \n", ret);
            break;
        }
        else
        {
           // printf("read data:\n %s ", buf);
            //检查是否有期望字段
            char *p = strstr(buf,name);
            if (p)
            {
                p += strlen(name) + 1;
                int i = 0;

                while( (*p != '\n') && (*p != '\0') && (i++ < value_len))
                {
                    *value++ = *p++;
                }

                ret_value = 0;
                break;
            }

        }
    }
    while(0);

exit:
    ret = unzCloseCurrentFile(uf);
    if (ret != UNZ_OK)
    {
       ALOGE("close current err %d\n", ret);
    }
    unzClose(uf);
    if (buf)
        free(buf);

    return ret_value;

}


int upgrade()
{

    const char update_file_name[] = LOCAL_UPGRADE_FILE_NAME;

    char real_file_path[1024];
    int ret = UPGRADE_OK;

    //检查文件是否存在
    if (access(update_file_name, F_OK) != 0)
    {
        ALOGE("%s not exist!\n",update_file_name);
        update_result = UPGRADE_UPGRADE_FAIL;
        return UPGRADE_ERR_FILE_ACCESS;
    }

    realpath(update_file_name, real_file_path);


    //写入/cache/recovey/last_flag
    WriteFlagCommand((char *)update_file_name);

    char command[1024];
    char locale[256];

    property_get("ro.product.locale",locale, "en-US");

    //判断是否 /data/开头
    if (memcmp(real_file_path,"/data/", 6) == 0)
    {
        unlink(UNCRYPT_PACKAGE_FILE);
        int fd;

        if ( ( fd = open(UNCRYPT_PACKAGE_FILE, O_RDWR|O_CREAT)) < 0  )
        {
            ALOGE("cant not open %s file , error %s !\n", UNCRYPT_PACKAGE_FILE, strerror(errno));

            update_result = UPGRADE_UPGRADE_FAIL;
            return  UPGRADE_ERR_UNCRYPT_FILE; 
        }
        
        write(fd, real_file_path, strlen(real_file_path));
        write(fd, "\n", 1);
        close(fd);

        unlink(BLOCK_MAP_FILE);


        //change file name
        sprintf(command, "--update_package=@/cache/recovery/block.map\n--locale=%s\n",locale);

    }
    else
    {
        sprintf(command, "--update_package=%s\n--locale=%s\n",real_file_path,locale);
    }

    ALOGD("setup command [%s]", command);

    if (!setupOrClearBcb(1,command))
    {
        ALOGE("setup Fail\n");
        update_result = UPGRADE_UPGRADE_FAIL;
        return UPGRADE_ERR_SETUP_BCB;
    }

    ALOGD("ready to reboot ...");

    if (memcmp(real_file_path,"/data/", 6) == 0)
    {
        sleep(3);
        if (uncrypt(real_file_path) == false)
        {
            ALOGD("uncrypt error\n");
            update_result = UPGRADE_UPGRADE_FAIL;
            return UPGRADE_ERR_UNCRYPT;

        }
    }


    printf("ready to reboot\n");
    property_set("sys.powerctl","reboot, recovery");
    return UPGRADE_OK;

}



static char *upgrade_down_url = NULL;
static pthread_t process_http_down_tid = 0;

static void (*mCallBack)(int ret) =  NULL;
static int  mFileSize = 0;

static void  result_call_back(int ret)
{
    //回调释放 url
    if (upgrade_down_url)
    {
        free(upgrade_down_url);
        upgrade_down_url = NULL; 
    }

    ALOGI("upgrade result call back  %d", ret);
    if (ret == 0)
    {
        update_result = UPGRADE_DOWNLOAD_COMPLETE;
        
        //启动http server
        property_set("ctl.start", "httpd_upgrade");
    }
    else
    {
        update_result = ret;
    }

    //回调外部调用
    if (mCallBack)
        mCallBack(!ret);

}

static void *process_httpdown(void *)
{
    prctl(PR_SET_NAME, "http_download ");
    update_result = UPGRADE_DOWNLOADING;

    unlink(LOCAL_UPGRADE_FILE_NAME);

    HttpClient::getInstance()->HttpGetNoBlock(upgrade_down_url, LOCAL_UPGRADE_FILE_NAME,NULL, result_call_back , mFileSize );

    return NULL;
}


int upgrade_download(char *down_url, int file_size, void callback_fun(int))
{

    //重复启动
    if (upgrade_down_url != NULL)
        return UPGRADE_ERR_IN_PROCESS;


    //输入参数非法
    if ( down_url == NULL )
        return UPGRADE_ERR_FILE_ACCESS;


    ALOGI("%s  down_url %s", __FUNCTION__, down_url);

    update_result = UPGRADE_DEFAULT_STATUS;

    mCallBack = callback_fun;
    mFileSize = file_size;

    mkdir(LOCAL_UPGRADE_FILE_PATH,0755);

    upgrade_down_url = (char *)malloc(strlen(down_url) + 1);
    memset(upgrade_down_url, 0, strlen(down_url) + 1);
    strcpy(upgrade_down_url, down_url);

    //启动线程线程
    pthread_create(&process_http_down_tid, NULL, process_httpdown, NULL );
        
    pthread_detach(process_http_down_tid);

    return UPGRADE_OK;
}


void upgrade_download_cancel(void)
{
    ALOGI("%s", __FUNCTION__);

    HttpClient::getInstance()->destroyInstance();

    //改为detach 方法非阻塞退出
//    pthread_join(process_http_down_tid,NULL);
    process_http_down_tid = 0;

    if (upgrade_down_url)
    {
        free(upgrade_down_url);
        upgrade_down_url = NULL;
    }
}


//返回升级状态
int upgrade_status_get(int *status, int *flag)
{
    int ret = -1;

    if (status)
        *status = update_result;

    if (flag)
    {
        char update_prefix[256];
        int bytes_read = 0;;
        int fd;

        
        if ( ( fd = open(UPDATE_FLAG_FILE, O_RDWR|O_CREAT)) < 0  )
        {
            ALOGE("Fail to open file '%s', error : '%s'.", UPDATE_FLAG_FILE, strerror(errno) );
            goto EXIT;
                
        }
            

        if ( ( bytes_read =  read(fd, update_prefix, sizeof(update_prefix) ) ) < 0  )
        {
            ALOGE("Fail to read  file '%s', bytes read : '%d', error : '%s'.", UPDATE_FLAG_FILE, bytes_read, strerror(errno) );
            goto EXIT;
        }

        ret = 0;
        if (memcmp(update_prefix, COMMAND_FLAG_SUCCESS,strlen(COMMAND_FLAG_SUCCESS)) == 0)
        {
            *flag = 1;
        }
        else if(memcmp(update_prefix, COMMAND_FLAG_UPDATING, strlen(COMMAND_FLAG_UPDATING)) == 0)
        {
            *flag = 0;
        }
        else
        {
            *flag = 2;
        }

EXIT:
        if ( fd > 0  )
        {
           close(fd);
        }
       
   }
    
    return ret;

}


void remove_download_file()
{
    unlink(LOCAL_UPGRADE_FILE_NAME);
}

