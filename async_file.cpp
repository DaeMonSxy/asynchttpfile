#include "async_file.h"

AsyncFile::AsyncFile() : client(nullptr) {}

AsyncFile::~AsyncFile() {
    if (client) {
        client->close(true);
        delete client;
    }
}

void AsyncFile::handleRequestCleanup(AsyncClient *client)
{
    if (client) {
        client->close(true);
        delete client;
    }
}

//--------------------------------------------------------------------------------

unsigned long AsyncFile::uploadFile(const char *auth, const char *host, int port, const char *path, const char *localFile)
{
    if (WiFi.status() != WL_CONNECTED)
        return 0;

    Serial.printf("[file] uploadFile: %s %i %s", host, port, path);

    FileData data;
    data.sendID = ++getID;

    strncpy(data.auth, auth, sizeof(data.auth));
    strncpy(data.host, host, sizeof(data.host));
    data.port = port;
    strncpy(data.path, path, sizeof(data.path));
    strncpy(data.localFile, localFile, sizeof(data.localFile));

    if (addFileToQueue(data)) {
        return getID;
    }
    return 0;
}

unsigned long AsyncFile::downloadFile(const char *auth, const char *host, int port, const char *path, const char *localFile)
{
    if (WiFi.status() != WL_CONNECTED)
        return 0;

    Serial.printf("[file] downloadFile: %s %i %s", host, port, path);

    FileData data;
    data.sendID = ++getID;

    strncpy(data.auth, auth, sizeof(data.auth));
    strncpy(data.host, host, sizeof(data.host));
    data.port = port;
    strncpy(data.path, path, sizeof(data.path));
    strncpy(data.localFile, localFile, sizeof(data.localFile));

    if (addFileToQueue(data)) {
        return getID;
    }
    return 0;
}

//--------------------------------------------------------------------------------

bool AsyncFile::addFileToQueue(const FileData &data)
{
    if (WiFi.status() == WL_CONNECTED) {
        if (fileDataList.size() >= MAX_FILE_DATA_LIST_SIZE) {
            fileDataList.pop_back();
        }

        for (const auto &item : fileDataList) {
            if (strncmp(item.auth, data.auth, sizeof(item.auth)) == 0 &&
                strncmp(item.host, data.host, sizeof(data.host)) == 0 &&
                item.port == data.port &&
                strncmp(item.path, data.path, sizeof(data.path)) == 0 &&
                strncmp(item.localFile, data.localFile, sizeof(data.localFile)) == 0) {
                return false; // Data already exists, so don't add it again
            }
        }

        fileDataList.push_front(data);
        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------

void AsyncFile::sendUploadRequest(unsigned long sendID, unsigned long timestamp, const FileData &data)
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    if (ESP.getFreeHeap() < 8000) {
        Serial.printf("[file] sendUploadRequest:ERR Heap below threshold");
        return;
    }

    if (!LittleFS.exists(data.localFile)) {
        Serial.printf("[file] ERR Local file not found: %s", data.localFile);
        return;
    }

    File file = LittleFS.open(data.localFile, "r");
    if (!file) {
        Serial.printf("[file] ERR Unable to open local file: %s", data.localFile);
        return;
    }

    AsyncClient *client = new AsyncClient();
    if (!client) {
        Serial.printf("[file] ## Failed to create client");
        file.close();
        return;
    }

    client->onError([this, sendID](void *arg, AsyncClient *client, err_t error) {
        Serial.printf("[file] ## [%lu] ERR %s\n", sendID, client->errorToString(error));
        handleRequestCleanup(client);
    }, nullptr);

    client->onDisconnect([this, sendID](void *arg, AsyncClient *client) {
        Serial.printf("[file] ## [%lu] onDisconnect", sendID);
        handleRequestCleanup(client);
    }, nullptr);

    client->onData([this, sendID](void *arg, AsyncClient *client, void *data, size_t len) {
        Serial.printf("[file] ## [%lu] onData", sendID);
        if (len > 0) {
            handleData(client, sendID, data, len);
        }
    }, nullptr);

    client->onConnect([this, data, sendID, file](void *arg, AsyncClient *client) mutable {
        Serial.printf("[file] Connected to %s", data.host);

        Serial.printf("[file] #> [%lu] %s:%i%s", data.sendID, data.host, data.port, data.path);

        char requestBuffer[1024];
        int formattedLength = snprintf(requestBuffer, sizeof(requestBuffer),
                                       "PUT %s HTTP/1.1\r\n"
                                       "Host: %s:%i\r\n"
                                       "Authorization: Basic %s\r\n"
                                       "User-Agent: iqESP\r\n"
                                       "Connection: close\r\n"
                                       "Content-Length: %u\r\n\r\n",
                                       data.path, data.host, data.port, data.auth, file.size());

        if (formattedLength < 0 || formattedLength >= (int)sizeof(requestBuffer)) {
            Serial.printf("[file] ERR - snprintf formatting error");
            file.close();
            return;
        }

        client->write(requestBuffer);

        size_t chunkSize = 512;
        uint8_t buffer[chunkSize];
        size_t bytesRead;

        while ((bytesRead = file.read(buffer, chunkSize)) > 0) {
            client->write(buffer, bytesRead);
        }

        file.close();
    }, nullptr);

    if (!client->connect(data.host, data.port)) {
        Serial.printf("[file] ## [%lu] ERR Failed to connect %s\n", sendID, data.host);
        file.close();
        handleRequestCleanup(client);
    }
}

void AsyncFile::sendDownloadRequest(unsigned long sendID, unsigned long timestamp, const FileData &data)
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    if (ESP.getFreeHeap() < 8000) {
        Serial.printf("[file] sendDownloadRequest:ERR Heap below threshold");
        return;
    }

    File file = LittleFS.open(data.localFile, "w");
    if (!file) {
        Serial.printf("[file] ERR Unable to open local file for writing: %s", data.localFile);
        return;
    }

    AsyncClient *client = new AsyncClient();
    if (!client) {
        Serial.printf("[file] ## Failed to create client");
        file.close();
        return;
    }

    client->onError([this, sendID](void *arg, AsyncClient *client, err_t error) {
        Serial.printf("[file] ## [%lu] ERR %s\n", sendID, client->errorToString(error));
        handleRequestCleanup(client);
    }, nullptr);

    client->onDisconnect([this, sendID](void *arg, AsyncClient *client) {
        Serial.printf("[file] ## [%lu] onDisconnect", sendID);
        handleRequestCleanup(client);
    }, nullptr);

    client->onData([this, sendID, file](void *arg, AsyncClient *client, void *data, size_t len) mutable {
        Serial.printf("[file] ## [%lu] onData", sendID);
        if (len > 0) {
            file.write((const uint8_t *)data, len);
        }
    }, nullptr);

    client->onConnect([this, data, sendID](void *arg, AsyncClient *client) {
        Serial.printf("[file] Connected to %s", data.host);

        Serial.printf("[file] #> [%lu] %s:%i%s", data.sendID, data.host, data.port, data.path);

        char requestBuffer[1024];
        int formattedLength = snprintf(requestBuffer, sizeof(requestBuffer),
                                       "GET %s HTTP/1.1\r\n"
                                       "Host: %s:%i\r\n"
                                       "Authorization: Basic %s\r\n"
                                       "User-Agent: iqESP\r\n"
                                       "Connection: close\r\n\r\n",
                                       data.path, data.host, data.port, data.auth);

        if (formattedLength < 0 || formattedLength >= (int)sizeof(requestBuffer)) {
            Serial.printf("[file] ERR - snprintf formatting error");
            return;
        }

        client->write(requestBuffer);
    }, nullptr);

    if (!client->connect(data.host, data.port)) {
        Serial.printf("[file] ## [%lu] ERR Failed to connect %s\n", sendID, data.host);
        file.close();
        handleRequestCleanup(client);
    }
}

//--------------------------------------------------------------------------------

void AsyncFile::onData_cb(std::function<void(unsigned long, const char *)> callback)
{
    onData_cb_ = callback;
}

void AsyncFile::onData_cb_json(std::function<void(unsigned long, JsonDocument &doc)> callback)
{
    onData_cb_json_ = callback;
}

void AsyncFile::handleData(AsyncClient *c, unsigned long sendID, void *data, size_t len)
{
    if (data == nullptr) {
        return;
    }

    const char *payloadStart = static_cast<const char *>(data);
    const char *jsonStart    = strstr(payloadStart, "{");
    if (!jsonStart)
        return;

    const char *jsonEnd = strrchr(payloadStart, '}');
    if (!jsonEnd || jsonEnd < jsonStart)
        return;

    size_t jsonLen = (jsonEnd - jsonStart) + 1;
    if (jsonLen > (size_t)(payloadStart + sizeof(data) - jsonStart))
        return;

    std::string jsonData(jsonStart, jsonLen);

    if (!jsonData.empty()) {
        jsonData.erase(std::remove_if(jsonData.begin(), jsonData.end(), [](char c) {
                           return std::isspace(static_cast<unsigned char>(c));
                       }),
                       jsonData.end());

        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, jsonData);
        if (error)
            Serial.printf("[file] ## [%lu] ERR json: %s", sendID, error.c_str());

        Serial.printf("[file] #< [%lu] %s", sendID, jsonData.c_str());
        if (onData_cb_json_)
            onData_cb_json_(sendID, doc);
    } else {
        Serial.printf("[file] [%lu] ERR response not JSON", sendID);
    }
}

//--------------------------------------------------------------------------------

void AsyncFile::loop()
{
    if (WiFi.status() != WL_CONNECTED || fileDataList.empty())
        return;

    static unsigned long lastExecutionTime = 0;
    unsigned long        currentTime       = millis();

    if (currentTime - lastExecutionTime >= 250) {
        lastExecutionTime = currentTime;

        auto oldestUnsent = fileDataList.begin();

        if (oldestUnsent != fileDataList.end()) {
            oldestUnsent->timestamp = millis();

            if (oldestUnsent->sendID) {
                if (oldestUnsent->host[0] != '\0' && oldestUnsent->path[0] != '\0' && oldestUnsent->localFile[0] != '\0') {
                    if (LittleFS.exists(oldestUnsent->localFile)) {
                        sendUploadRequest(oldestUnsent->sendID, oldestUnsent->timestamp, *oldestUnsent);
                    } else {
                        sendDownloadRequest(oldestUnsent->sendID, oldestUnsent->timestamp, *oldestUnsent);
                    }
                } else {
                    Serial.printf("[file] ## ERR Incomplete data, skipping request");
                }
            }

            fileDataList.erase(oldestUnsent);
        }
    }
}
