#include "Updater.h"
#include "cocos2d.h"

#if (CC_TARGET_PLATFORM != CC_PLATFORM_WINRT) && (CC_TARGET_PLATFORM != CC_PLATFORM_WP8)
#include <curl/curl.h>
#include <curl/easy.h>

#include <stdio.h>
#include <vector>

#if (CC_TARGET_PLATFORM != CC_PLATFORM_WIN32)
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#endif

#include "support/zip_support/unzip.h"
#include "script_support/CCScriptSupport.h"

using namespace cocos2d;
using namespace std;

NS_CC_EXT_BEGIN;

#define KEY_OF_VERSION   "current-version-code"
#define KEY_OF_DOWNLOADED_VERSION    "downloaded-version-code"
#define TEMP_PACKAGE_FILE_NAME    "cocos2dx-update-temp-package.zip"
#define BUFFER_SIZE    8192
#define MAX_FILENAME   512

// Message type
#define UPDATER_MESSAGE_UPDATE_SUCCEED                0
#define UPDATER_MESSAGE_STATE                         1
#define UPDATER_MESSAGE_PROGRESS                      2
#define UPDATER_MESSAGE_ERROR                         3


// Some data struct for sending messages

struct ErrorMessage
{
    Updater::ErrorCode code;
    Updater* manager;
};

struct ProgressMessage
{
    int percent;
    Updater* manager;
};

struct StateMessage
{
    Updater::StateCode code;
    Updater* manager;
};

// Implementation of Updater

Updater::Updater()
: _curl(NULL)
, _tid(NULL)
, _connectionTimeout(0)
, _delegate(NULL)
, _scriptHandler(0)
, _zipUrl("")
, _zipFile("")
, _unzipTmpDir("")
, _updateInfoString("")
, _resetBeforeUnZip(true)
{
    _schedule = new Helper();
}

Updater::~Updater()
{
    if (_schedule)
    {
        _schedule->release();
    }
    unregisterScriptHandler();
}

void Updater::checkUnZipTmpDir()
{
    if (_unzipTmpDir.size() > 0 && _unzipTmpDir[_unzipTmpDir.size() - 1] != '/')
    {
        _unzipTmpDir.append("/");
    }
}

static size_t getUpdateInfoFun(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    string *updateInfo = (string*)userdata;
	CCLOG("updateInfo:%s", updateInfo->c_str());
    updateInfo->append((char*)ptr, size * nmemb);
    
    return (size * nmemb);
}

const char* Updater::getUpdateInfo(const char* url)
{
    CCLOG("Updater::getUpdateInfo(%s)", url);
    _curl = curl_easy_init();
    if (! _curl)
    {
        CCLOG("can not init curl");
        return "";
    }
    
    _updateInfoString.clear();
    
    CURLcode res;
    curl_easy_setopt(_curl, CURLOPT_URL, url);
    curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, getUpdateInfoFun);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &_updateInfoString);
    if (_connectionTimeout) curl_easy_setopt(_curl, CURLOPT_CONNECTTIMEOUT, _connectionTimeout);
    res = curl_easy_perform(_curl);
    
    if (res != 0)
    {
        sendErrorMessage(kNetwork);
        CCLOG("can not get version file content %s, error code is %d", url, res);
        curl_easy_cleanup(_curl);
        return "";
    }
    
    return _updateInfoString.c_str();
}

void* assetsManagerDownloadAndUncompress1(void *data)
{
    Updater* self = (Updater*)data;
    
    do
    {
        if (! self->downLoad(self->_zipUrl.c_str(), self->_zipFile.c_str())) break;
        
        // Uncompress zip file.
        if (! self->uncompress(self->_zipFile.c_str(), self->_unzipTmpDir.c_str(),self->_resetBeforeUnZip))
        {
            self->sendErrorMessage(Updater::kUncompress);
            break;
        }
        
        // Record updated version and remove downloaded zip file
        Updater::Message *msg2 = new Updater::Message();
        msg2->what = UPDATER_MESSAGE_UPDATE_SUCCEED;
        msg2->obj = self;
        self->_schedule->sendMessage(msg2);
    } while (0);
    
    if (self->_tid)
    {
        delete self->_tid;
        self->_tid = NULL;
    }
    
    return NULL;
}

void Updater::update(const char* zipUrl, const char* zipFile, const char* unzipTmpDir, bool resetBeforeUnZip)
{
    if (_tid) return;
    
    _zipUrl.clear();
    _zipUrl.append(zipUrl);
    _zipFile.clear();
    _zipFile.append(zipFile);
    _unzipTmpDir.clear();
    _unzipTmpDir.append(unzipTmpDir);
    _resetBeforeUnZip = resetBeforeUnZip;
    
    checkUnZipTmpDir();
    
    // 1. Urls of package and version should be valid;
    // 2. Package should be a zip file.
    if (_zipUrl.size() == 0 ||
        _zipFile.size() == 0 ||
        std::string::npos == _zipUrl.find(".zip"))
    {
        CCLOG("no version file url, or no package url, or the package is not a zip file");
        return;
    }
    
    _tid = new pthread_t();
    pthread_create(&(*_tid), NULL, assetsManagerDownloadAndUncompress1, this);
}

bool Updater::uncompress(const char* zipFilePath, const char* unzipTmpDir, bool resetBeforeUnZip)
{
    if(resetBeforeUnZip)
    {
        // Create unzipTmpDir
        if(CCFileUtils::sharedFileUtils()->isFileExist(unzipTmpDir))
        {
            this->removeDirectory(unzipTmpDir);        }
    }
    
    this->createDirectory(unzipTmpDir);
    
    // Open the zip file
    string outFileName = std::string(zipFilePath);
    unzFile zipfile = unzOpen(outFileName.c_str());
    if (! zipfile)
    {
        CCLOG("can not open downloaded zip file %s", outFileName.c_str());
        return false;
    }
    
    // Get info about the zip file
    unz_global_info global_info;
    if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK)
    {
        CCLOG("can not read file global info of %s", outFileName.c_str());
        unzClose(zipfile);
        return false;
    }
    
    // Buffer to hold data read from the zip file
    char readBuffer[BUFFER_SIZE];
    
    CCLOG("start uncompressing");
    this->sendStateMessage(kUncompressStart);

    // Loop to extract all files.
    uLong i;
    for (i = 0; i < global_info.number_entry; ++i)
    {
        // Get info about current file.
        unz_file_info fileInfo;
        char fileName[MAX_FILENAME];
        if (unzGetCurrentFileInfo(zipfile,
                                  &fileInfo,
                                  fileName,
                                  MAX_FILENAME,
                                  NULL,
                                  0,
                                  NULL,
                                  0) != UNZ_OK)
        {
            CCLOG("can not read file info");
            unzClose(zipfile);
            return false;
        }
        
        string fullPath = std::string(unzipTmpDir) + fileName;
        
        // Check if this entry is a directory or a file.
        const size_t filenameLength = strlen(fileName);
        if (fileName[filenameLength-1] == '/')
        {
            // Entry is a direcotry, so create it.
            // If the directory exists, it will failed scilently.
            if (!createDirectory(fullPath.c_str()))
            {
                CCLOG("can not create directory %s", fullPath.c_str());
                unzClose(zipfile);
                return false;
            }
        }
        else
        {
            //There are not directory entry in some case.
            //So we need to test whether the file directory exists when uncompressing file entry
            //, if does not exist then create directory
            const string fileNameStr(fileName);
            
            size_t startIndex=0;
            
            size_t index=fileNameStr.find("/",startIndex);
            
            while(index != std::string::npos)
            {
                const string dir=std::string(unzipTmpDir)+fileNameStr.substr(0,index);
                
                FILE *out = fopen(dir.c_str(), "r");
                
                if(!out)
                {
                    if (!createDirectory(dir.c_str()))
                    {
                        CCLOG("can not create directory %s", dir.c_str());
                        unzClose(zipfile);
                        return false;
                    }
                    else
                    {
                        CCLOG("create directory %s",dir.c_str());
                    }
                }
                else
                {
                    fclose(out);
                }
                
                startIndex=index+1;
                
                index=fileNameStr.find("/",startIndex);
                
            }
            
            
            
            // Entry is a file, so extract it.
            
            // Open current file.
            if (unzOpenCurrentFile(zipfile) != UNZ_OK)
            {
                CCLOG("can not open file %s", fileName);
                unzClose(zipfile);
                return false;
            }
            
            // Create a file to store current file.
            FILE *out = fopen(fullPath.c_str(), "wb");
            if (! out)
            {
                CCLOG("can not open destination file %s", fullPath.c_str());
                unzCloseCurrentFile(zipfile);
                unzClose(zipfile);
                return false;
            }
            
            // Write current file content to destinate file.
            int error = UNZ_OK;
            do
            {
                error = unzReadCurrentFile(zipfile, readBuffer, BUFFER_SIZE);
                if (error < 0)
                {
                    CCLOG("can not read zip file %s, error code is %d", fileName, error);
                    unzCloseCurrentFile(zipfile);
                    unzClose(zipfile);
                    return false;
                }
                
                if (error > 0)
                {
                    fwrite(readBuffer, error, 1, out);
                }
            } while(error > 0);
            
            fclose(out);
        }
        
        unzCloseCurrentFile(zipfile);
        
        // Goto next entry listed in the zip file.
        if ((i+1) < global_info.number_entry)
        {
            if (unzGoToNextFile(zipfile) != UNZ_OK)
            {
                CCLOG("can not read next file");
                unzClose(zipfile);
                return false;
            }
        }
    }
    
    CCLOG("end uncompressing");
    this->sendStateMessage(kUncompressDone);
    unzClose(zipfile);
    
    return true;
}

bool Updater::removeDirectory(const char* path)
{
    int succ = -1;
#if (CC_TARGET_PLATFORM != CC_PLATFORM_WIN32)
    string command = "rm -r ";
    // Path may include space.
    command += "\"" + string(path) + "\"";
    succ = system(command.c_str());
#else
    string command = "rd /s /q ";
    // Path may include space.
    command += "\"" + string(path) + "\"";
    succ = system(command.c_str());
#endif
    if(succ != 0)
    {
        return false;
    }
    return true;
}

/*
 * Create a direcotry is platform depended.
 */
bool Updater::createDirectory(const char *path)
{
#if (CC_TARGET_PLATFORM != CC_PLATFORM_WIN32)
    mode_t processMask = umask(0);
    int ret = mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
    umask(processMask);
    if (ret != 0 && (errno != EEXIST))
    {
        return false;
    }
    
    return true;
#else
    BOOL ret = CreateDirectoryA(path, NULL);
	if (!ret && ERROR_ALREADY_EXISTS != GetLastError())
	{
		return false;
	}
    return true;
#endif
}

static size_t downLoadPackage(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    FILE *fp = (FILE*)userdata;
    size_t written = fwrite(ptr, size, nmemb, fp);
    return written;
}

int assetsManagerProgressFunc1(void *ptr, double totalToDownload, double nowDownloaded,
                              double totalToUpLoad, double nowUpLoaded)
{
    Updater* manager = (Updater*)ptr;
    Updater::Message *msg = new Updater::Message();
    msg->what = UPDATER_MESSAGE_PROGRESS;
    
    ProgressMessage *progressData = new ProgressMessage();
    progressData->percent = (unsigned  int)(nowDownloaded/totalToDownload*100);
    progressData->manager = manager;
    msg->obj = progressData;
    
    manager->_schedule->sendMessage(msg);
    
    CCLOG("downloading... %d%%", progressData->percent);
    
    return 0;
}

bool Updater::downLoad(const char* zipUrl, const char* zipFile)
{
    // Create a file to save package.
    string outFileName = string(zipFile);
    FILE *fp = fopen(outFileName.c_str(), "wb");
    if (! fp)
    {
        sendErrorMessage(kCreateFile);
        CCLOG("can not create file %s", outFileName.c_str());
        return false;
    }
    
    this->sendStateMessage(kDownStart);
    _curl = curl_easy_init();
    // Download pacakge
    CURLcode res;
    curl_easy_setopt(_curl, CURLOPT_URL, zipUrl);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, downLoadPackage);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(_curl, CURLOPT_NOPROGRESS, false);
    curl_easy_setopt(_curl, CURLOPT_PROGRESSFUNCTION, assetsManagerProgressFunc1);
    curl_easy_setopt(_curl, CURLOPT_PROGRESSDATA, this);
    res = curl_easy_perform(_curl);
    curl_easy_cleanup(_curl);
    if (res != 0)
    {
        sendErrorMessage(kNetwork);
        CCLOG("error when download package");
        fclose(fp);
        return false;
    }
    
    CCLOG("succeed downloading package %s", zipUrl);
    
    fclose(fp);
    this->sendStateMessage(kDownDone);
    return true;
}

void Updater::setDelegate(UpdaterDelegateProtocol *delegate)
{
    _delegate = delegate;
}

void Updater::registerScriptHandler(int handler)
{
    unregisterScriptHandler();
    _scriptHandler = handler;
}

void Updater::unregisterScriptHandler(void)
{
    CCScriptEngineManager::sharedManager()->getScriptEngine()->
        removeScriptHandler(_scriptHandler);
    _scriptHandler = 0;
}

void Updater::setConnectionTimeout(unsigned int timeout)
{
    _connectionTimeout = timeout;
}

unsigned int Updater::getConnectionTimeout()
{
    return _connectionTimeout;
}

void Updater::sendErrorMessage(Updater::ErrorCode code)
{
    Message *msg = new Message();
    msg->what = UPDATER_MESSAGE_ERROR;
    
    ErrorMessage *errorMessage = new ErrorMessage();
    errorMessage->code = code;
    errorMessage->manager = this;
    msg->obj = errorMessage;
    
    _schedule->sendMessage(msg);
}

void Updater::sendStateMessage(Updater::StateCode code)
{
    Message *msg = new Message();
    msg->what = UPDATER_MESSAGE_STATE;
    
    StateMessage *stateMessage = new StateMessage();
    stateMessage->code = code;
    stateMessage->manager = this;
    msg->obj = stateMessage;
    
    _schedule->sendMessage(msg);
}

// Implementation of UpdaterHelper

Updater::Helper::Helper()
{
    _messageQueue = new list<Message*>();
    pthread_mutex_init(&_messageQueueMutex, NULL);
    CCDirector::sharedDirector()->getScheduler()
        ->scheduleUpdateForTarget(this, 0, false);
}

Updater::Helper::~Helper()
{
    CCDirector::sharedDirector()->getScheduler()
        ->unscheduleAllForTarget(this);
    delete _messageQueue;
}

void Updater::Helper::sendMessage(Message *msg)
{
    pthread_mutex_lock(&_messageQueueMutex);
    _messageQueue->push_back(msg);
    pthread_mutex_unlock(&_messageQueueMutex);
}

void Updater::Helper::update(float dt)
{
    Message *msg = NULL;
    
    // Returns quickly if no message
    pthread_mutex_lock(&_messageQueueMutex);
    if (0 == _messageQueue->size())
    {
        pthread_mutex_unlock(&_messageQueueMutex);
        return;
    }
    
    // Gets message
    msg = *(_messageQueue->begin());
    _messageQueue->pop_front();
    pthread_mutex_unlock(&_messageQueueMutex);
    
    switch (msg->what) {
        case UPDATER_MESSAGE_UPDATE_SUCCEED:
            handleUpdateSucceed(msg);
            break;
        case UPDATER_MESSAGE_STATE:
            handlerState(msg);
            break;
        case UPDATER_MESSAGE_PROGRESS:
            handlerProgress(msg);
            break;
        case UPDATER_MESSAGE_ERROR:
            handlerError(msg);
            break;
        default:
            break;
    }
    
    delete msg;
}

void Updater::Helper::handleUpdateSucceed(Message *msg)
{
    Updater* manager = (Updater*)msg->obj;
    
    // Delete unloaded zip file.
    string zipfileName = manager->_zipFile;
    if (remove(zipfileName.c_str()) != 0)
    {
        CCLOG("can not remove downloaded zip file %s", zipfileName.c_str());
    }
    
    if (manager)
    {
        if (manager->_delegate)
        {
            manager->_delegate->onSuccess();
        }
        if (manager->_scriptHandler)
        {
            CCScriptEngineManager::sharedManager()
                ->getScriptEngine()
                ->executeEvent(
                               manager->_scriptHandler,
                               "success",
                               CCString::create("success"),
                               "CCString"
                               );
        }
    }
}

void Updater::Helper::handlerState(Message *msg)
{
    StateMessage* stateMsg = (StateMessage*)msg->obj;
    if(stateMsg->manager->_delegate)
    {
        stateMsg->manager->_delegate->onState(stateMsg->code);
    }
    if (stateMsg->manager->_scriptHandler)
    {
        std::string stateMessage;
        switch ((StateCode)stateMsg->code)
        {
            case kDownStart:
                stateMessage = "downloadStart";
                break;
                
            case kDownDone:
                stateMessage = "downloadDone";
                break;
                
            case kUncompressStart:
                stateMessage = "uncompressStart";
                break;
            case kUncompressDone:
                stateMessage = "uncompressDone";
                break;
                
            default:
                stateMessage = "stateUnknown";
        }
        
        CCScriptEngineManager::sharedManager()
            ->getScriptEngine()
            ->executeEvent(
                           stateMsg->manager->_scriptHandler,
                           "state",
                           CCString::create(stateMessage.c_str()),
                           "CCString");
    }
    
    delete ((StateMessage*)msg->obj);
}

void Updater::Helper::handlerError(Message* msg)
{
    ErrorMessage* errorMsg = (ErrorMessage*)msg->obj;
    if (errorMsg->manager->_delegate)
    {
        errorMsg->manager->_delegate
            ->onError(errorMsg->code);
    }
    if (errorMsg->manager->_scriptHandler)
    {
        std::string errorMessage;
        switch (errorMsg->code)
        {
            case kCreateFile:
                errorMessage = "errorCreateFile";
                break;
                
            case kNetwork:
                errorMessage = "errorNetwork";
                break;
                
            case kNoNewVersion:
                errorMessage = "errorNoNewVersion";
                break;
                
            case kUncompress:
                errorMessage = "errorUncompress";
                break;
                
            default:
                errorMessage = "errorUnknown";
        }
        
        CCScriptEngineManager::sharedManager()
            ->getScriptEngine()
            ->executeEvent(
                           errorMsg->manager->_scriptHandler,
                           "error",
                           CCString::create(errorMessage.c_str()),
                           "CCString"
                           );
    }
    
    delete ((ErrorMessage*)msg->obj);
}

void Updater::Helper::handlerProgress(Message* msg)
{
    ProgressMessage* progMsg = (ProgressMessage*)msg->obj;
    if (progMsg->manager->_delegate)
    {
        progMsg->manager->_delegate
            ->onProgress(progMsg->percent);
    }
    if (progMsg->manager->_scriptHandler)
    {
        //char buff[10];
        //sprintf(buff, "%d", ((ProgressMessage*)msg->obj)->percent);
        CCScriptEngineManager::sharedManager()
            ->getScriptEngine()
            ->executeEvent(
                       progMsg->manager->_scriptHandler,
                       "progress",
                       CCInteger::create(progMsg->percent),
                       "CCInteger"
                       );
    }
    
    delete (ProgressMessage*)msg->obj;
}

NS_CC_EXT_END;
#endif // CC_PLATFORM_WINRT