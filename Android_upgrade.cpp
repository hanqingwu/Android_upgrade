/*************************************************************************
	> File Name: Ruijie_upgrade.cpp
	> Author: 
	> Mail: 
	> Created Time: 2018年03月19日 星期一 14时14分07秒
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

using namespace std;

#undef LOG_TAG 
#define LOG_NDDBUG 0
#define LOG_TAG  "upgrade"
#include  <utils/Log.h>


#define POSITION_OF_PRODUCT_NAME    (0x08)
#define MAX_PRODUCT_NAME_LENGTH     (64)

#define POSITION_OF_VERSION_INFO    (0x84)
#define MAX_VERSION_LENGTH          (4)


#define UPDATE_FLAG_FILE    "/cache/recovery/last_flag"

#define UNCRYPT_PACKAGE_FILE "/cache/recovery/uncrypt_file" 
#define BLOCK_MAP_FILE      "/cache/recovery/block.map"

#define UNIX_DOMAIN   "/dev/socket/uncrypt"


#define USB_STORAGE_PATH   "/mnt/media_rw/"

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

        
      //  printf("wait connect %d second, ret %d, errno %d \n", i, ret, errno);
        sleep(1);
       
    }

    if (ret < 0)
    {  
        ALOGE("connect server fail ,errno %d", errno);
        printf("connect server fail\n");
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
        //printf("read len %d  status %d, buf %x %x %x %x \n", length, status, buf[0], buf[1], buf[2], buf[3]);
        printf("status %d\n", status);
        ALOGD("read status %d", status);

        if (status == 100)
        {
            ret = true;
            status = 0;
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

        
//        printf("wait connect %d second, ret %d, errno %d \n", i, ret, errno);
        sleep(1);
       
    }

    if (ret < 0)
    {  
        ALOGE("connect server fail ,errno %d", errno);
        printf("connect server fail\n");
        close(socket_fd);
        return false;
    }

//    printf("connect ok\n ");
//    ALOGD("connect ok");

    if (isSetup)
    {
        int command_length = strlen(command);
        memset(buf, 0, sizeof(1024));    
        int net_len = htonl(command_length);

        memcpy(buf,&net_len,4);
        strcpy(buf + 4, command);

        /*
        printf("write %d :\n", command_length + 4);
        for (int i = 0; i < command_length + 4; i++)
        {
            printf("%c(%02x) ", buf[i], buf[i]);
        }
        */

        write(socket_fd, buf, command_length + 4);
    }

    int status;
    int length = 0;
    length  =  read(socket_fd, buf, 4);

    status = ntohl(*(uint32_t *)buf);
//    printf("read len %d  status %d, buf %x %x %x %x \n", length, status, buf[0], buf[1], buf[2], buf[3]);
    
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


int copy_file(const char *src_path, const char *des_path)
{
    int len;
    int fd,fd2;
    int ret = -1;
    char buff[2048];
 
    fd = open(src_path,O_RDWR);
    if (fd < 0)
    {
        ALOGE("%s open fail !", src_path);
        return -1;
    }

    fd2 = open(des_path,O_RDWR|O_CREAT);
    if (fd2 < 0)
    {
        ALOGE("%s open fail !", des_path);
        goto exit;
    }

    while(len = read(fd,buff,sizeof(buff)))
    {
        write(fd2,buff,len);
    }

    ret = 0;

exit:

    close(fd);
    close(fd2);

    return ret;
}





int main(int argc, char **argv)
{
    char *update_file_name = NULL;
    char target_file_name[512];
    char real_file_path[1024];
    bool  flag_need_copy_upgrade = false; //用于标记是否要做复制到/data下的动作

    //检查参数
    if (argc < 2)
    {
        //自动到U盘去搜索
        DIR *dp;
        struct dirent *ps;

        dp = opendir(USB_STORAGE_PATH);
        if (dp != NULL)
        {
            while((ps = readdir(dp)) != NULL)
            {
                if (strcmp(ps->d_name,".")  == 0)
                    continue;

                if (strcmp(ps->d_name,"..")  == 0)
                    continue;

                printf("found %s\n", ps->d_name);
                //枪查该目录下是否有<product name>_update.zip
                sprintf(target_file_name, "%s%s/update.zip", USB_STORAGE_PATH, ps->d_name);
                if (access(target_file_name, F_OK) == 0)
                {
                    printf("found update.zip in %s \n", target_file_name);
                    update_file_name = target_file_name;

                    break;
                }
                
                //第一次没找到，需要二次查找,则认为需要复制了
                flag_need_copy_upgrade = true;
            }

            closedir(dp);
        }

        if (update_file_name == NULL)
        {
            printf("\n\tusage: \n");
            printf("\tupgrade [filename]\n");
            printf("\texample USB device:\n");
            printf("\tupgrade /mnt/media_rw/xxxxx/update.zip\n");
            printf("\tif filename not input , we will search automatically in usb storage!\n");
        
            return 0;
        }
    }
    else
    {
        update_file_name = argv[1];
    }

    ALOGD("flag_need_copy_upgrade  %d\r\n", flag_need_copy_upgrade);

    //检查文件是否存在
    if (access(update_file_name, F_OK) != 0)
    {
        printf("%s not exist!\n",update_file_name);
        return -1;
    }

    
    char command[1024];
    char locale[256];

   
    property_get("ro.product.locale",locale, "en-US");

    //文件枪查通过，如果要复制，就要开始了
    if (flag_need_copy_upgrade)
    {
        ALOGI("start copy file to flash");
        printf("start copy file to flash \n");
        mkdir(LOCAL_UPGRADE_FILE_PATH,0755);
        if ( copy_file(update_file_name,LOCAL_UPGRADE_FILE_NAME) < 0)
        {
            ALOGE("copy file fail!");
            goto exit;
        }

        strcpy(update_file_name, LOCAL_UPGRADE_FILE_NAME);
    }

    
    realpath(update_file_name, real_file_path);

    //写入/cache/recovey/last_flag
    WriteFlagCommand(real_file_path);


    //判断是否 /data/开头
    if (memcmp(real_file_path,"/data/", 6) == 0)
    {
        unlink(UNCRYPT_PACKAGE_FILE);
        int fd;

        if ( ( fd = open(UNCRYPT_PACKAGE_FILE, O_RDWR|O_CREAT)) < 0  )
        {
            printf("cant not open %s file , error %s !\n", UNCRYPT_PACKAGE_FILE, strerror(errno));
            goto exit;
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
        printf("setup Fail\n");
        printf("please retry again !\n");
        return -1;
    }

    if (memcmp(real_file_path,"/data/", 6) == 0)
    {
        sleep(3);
        if (uncrypt(real_file_path) == false)
        {
            printf("uncrypt error\n");
            ALOGE("uncrypt error");
            return -1;
        }   
    }

    property_set("sys.powerctl","reboot, recovery");
    return 0;
exit:

    return -1;
}
