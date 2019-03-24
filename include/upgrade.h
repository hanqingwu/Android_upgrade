/*************************************************************************
	> File Name: include/upgrade.h
	> Author: 
	> Mail: 
	> Created Time: 2018年06月08日 星期五 17时22分19秒
 ************************************************************************/

#ifndef UPGRADE_H
#define UPGRADE_H
#ifdef __cplusplus
extern "C" {
#endif


#define  LOCAL_UPGRADE_FILE_PATH      "/data/www"
#define  LOCAL_UPGRADE_FILE_NAME      "/data/www/update.zip"

enum
{
    UPGRADE_OK = 0,

    //upgrade_download 返回错误类型
    UPGRADE_ERR_FILE_ACCESS,   //down_url 文件不存在
    UPGRADE_ERR_FILE_SIZE,      //下载文件大小不一致。
    UPGRADE_ERR_IN_PROCESS,      //重复启动
    //upgrade 返回错误类型
    UPGRADE_ERR_PRODUCT_NAME,
    UPGRADE_ERR_UNCRYPT_FILE,
    UPGRADE_ERR_SETUP_BCB,
    UPGRADE_ERR_UNCRYPT

};

/*
return：
0-  初始默认状态（该状态下没有启动任何升级动作）;
1-	正在下载固件（表示正在下载固件）；
2-	下载完成正在升级固件；
3-	升级完成，且升级成功；
4-	升级完成，且升级失败（失败原因后续可扩展）；
5-  失败原因
*/
enum
{
    UPGRADE_DEFAULT_STATUS = 0,
    UPGRADE_DOWNLOADING,
    UPGRADE_DOWNLOAD_COMPLETE,
    UPGRADE_UPGRADE_SUCCESS,
    UPGRADE_UPGRADE_FAIL,
    UPGRADE_DOWNLOAD_FAIL_REMOTE_ACCESS,     //远程文件不存在
    UPGRADE_DOWNLOAD_FAIL_FILE_REMOTE_SIZE,      //远程文件大小与预期不一致
    UPGRADE_DOWNLOAD_FAIL_CURL_INIT,   //curl 下载库初始化失败
    UPGRADE_DOWNLOAD_FAIL_LOCAL_ACCESS,     //本地下载文件创建失败
    UPGRADE_DOWNLOAD_FAIL_POSITIVE_CANCEL,   //主动取消下载
    UPGRADE_DOWNLOAD_FAIL_NETWORK,           //网络原因下载失败，
    UPGRADE_DOWNLOAD_FAIL_LOCAL_SIZE,           //下载本地文件大小与预期不一致

};
/*  
  获取升级状态
*/
int upgrade_status_get(int *status ,int *flag);

/*
  非阻塞，下载路径和文件名管理，upgrade库维护。
  @url:小于128
  return：返回0：成功，其他：失败（正在下载、url错误，你那边看下还有没其他）

  下载完成后,提供http_server ， http://ip:8080/update.zip 
*/
int upgrade_download(char *down_url, int file_size, void callback_fun(int));
//0下载失败，1下载成功))

/*
  非阻塞，满足条件则重启
  return: 0:成功，其他：失败
*/
int upgrade(void);

/*
  强制断开下载
*/
void upgrade_download_cancel(void);


/*
 * 删除已下载安装包
 *
*/
void remove_download_file();

#ifdef __cplusplus
}
#endif
#endif
