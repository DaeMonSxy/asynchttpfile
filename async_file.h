#ifndef ASYNC_FILE_H
#define ASYNC_FILE_H

#include <string>
#include <list>
#include <vector>
#include <LittleFS.h>
#include <ArduinoJson.h>

#if defined(ESP8266)
    #include <ESPAsyncTCP.h>
    #include <ESP8266WiFi.h>
#elif defined(ESP32)
    #include <AsyncTCP.h>
    #include <WiFi.h>
#endif

class AsyncFile
{
   public:
    AsyncFile();
    ~AsyncFile();

    void loop();

    unsigned long uploadFile(const char *auth, const char *host, int port, const char *path, const char *localFile);
    unsigned long downloadFile(const char *auth, const char *host, int port, const char *path, const char *localFile);

    void onData_cb(std::function<void(unsigned long, const char *)> callback);
    void onData_cb_json(std::function<void(unsigned long, JsonDocument &doc)> callback);

   private:
    AsyncClient *client;

    struct FileData
    {
        char          auth[64];
        char          host[64];
        int           port;
        char          path[400];
        char          localFile[128];
        unsigned long sendID     = 0;
        unsigned long timestamp = 0;
    };

    std::list<FileData> fileDataList;
    unsigned long       getID = 0;

    bool addFileToQueue(const FileData &data);
    void sendUploadRequest(unsigned long sendID, unsigned long timestamp, const FileData &data);
    void sendDownloadRequest(unsigned long sendID, unsigned long timestamp, const FileData &data);
    void handleRequestCleanup(AsyncClient *client);

    std::function<void(unsigned long, const char *)>      onData_cb_;
    std::function<void(unsigned long, JsonDocument &doc)> onData_cb_json_;

    static const size_t MAX_FILE_DATA_LIST_SIZE = 10;
};

#endif // ASYNC_FILE_H
