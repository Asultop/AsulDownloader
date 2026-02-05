#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QUrl>
#include "AsulMultiDownloader.h"

void parseAndDownloadAssets(AsulMultiDownloader *downloader, const QString &assetsJsonPath);
void parseAndDownloadVersion(AsulMultiDownloader *downloader, const QString &versionJsonPath);

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    qDebug() << "AsulDownloader - Minecraft Asset and Version Downloader";
    qDebug() << "========================================================";
    
    // Create downloader instance
    AsulMultiDownloader downloader;
    
    // Configure downloader for optimal performance
    downloader.setMaxConcurrentDownloads(16);  // Higher concurrency for small files
    downloader.setLargeFileThreshold(10 * 1024 * 1024);  // 10MB threshold
    downloader.setSegmentCountForLargeFile(4);  // 4 segments for large files
    
    // Track completion
    int totalTasks = 0;
    int completedTasks = 0;
    int failedTasks = 0;
    
    // Connect signals to track progress
    QObject::connect(&downloader, &AsulMultiDownloader::downloadFinished,
                     [&](const QString &taskId, const QString &savePath) {
        completedTasks++;
        qDebug() << QString("Completed (%1/%2): %3")
                    .arg(completedTasks)
                    .arg(totalTasks)
                    .arg(QFileInfo(savePath).fileName());
    });
    
    QObject::connect(&downloader, &AsulMultiDownloader::downloadFailed,
                     [&](const QString &taskId, const QString &errorString) {
        failedTasks++;
        qDebug() << "Failed:" << errorString;
    });
    
    QObject::connect(&downloader, &AsulMultiDownloader::allDownloadsFinished,
                     [&]() {
        qDebug() << "\n========================================================";
        qDebug() << "All downloads finished!";
        qDebug() << QString("Completed: %1, Failed: %2").arg(completedTasks).arg(failedTasks);
        qDebug() << "========================================================";
        QCoreApplication::quit();
    });
    
    // Parse and download assets
    qDebug() << "\nParsing assets.json...";
    parseAndDownloadAssets(&downloader, "assets.json");
    
    // Parse and download version files
    qDebug() << "\nParsing version.json...";
    parseAndDownloadVersion(&downloader, "version.json");
    
    // Get total task count
    totalTasks = downloader.getAllTaskIds().count();
    qDebug() << QString("\nTotal tasks added: %1").arg(totalTasks);
    qDebug() << "Starting downloads...\n";

    return a.exec();
}

void parseAndDownloadAssets(AsulMultiDownloader *downloader, const QString &assetsJsonPath)
{
    // Read assets.json
    QFile file(assetsJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open assets.json:" << file.errorString();
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "Invalid JSON format in assets.json";
        return;
    }
    
    QJsonObject root = doc.object();
    QJsonObject objects = root["objects"].toObject();
    
    qDebug() << QString("Found %1 asset objects").arg(objects.size());
    
    // Prepare base directory
    QDir().mkpath("minecraft/assets/objects");
    
    int count = 0;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        QJsonObject assetObj = it.value().toObject();
        QString hash = assetObj["hash"].toString();
        qint64 size = assetObj["size"].toVariant().toLongLong();
        
        if (hash.isEmpty()) {
            continue;
        }
        
        // Create URL: https://resources.download.minecraft.net/{first 2 chars}/{hash}
        QString hashPrefix = hash.left(2);
        QString url = QString("https://resources.download.minecraft.net/%1/%2")
                        .arg(hashPrefix)
                        .arg(hash);
        
        // Create local path: ./minecraft/assets/objects/{first 2 chars}/{hash}
        QString localDir = QString("minecraft/assets/objects/%1").arg(hashPrefix);
        QDir().mkpath(localDir);
        QString localPath = QString("%1/%2").arg(localDir).arg(hash);
        
        // Check if file already exists with correct size
        QFileInfo fileInfo(localPath);
        if (fileInfo.exists() && fileInfo.size() == size) {
            continue;  // Skip already downloaded files
        }
        
        // Add download task
        downloader->addDownload(QUrl(url), localPath, 0);
        count++;
    }
    
    qDebug() << QString("Added %1 new asset download tasks").arg(count);
}

void parseAndDownloadVersion(AsulMultiDownloader *downloader, const QString &versionJsonPath)
{
    // Read version.json
    QFile file(versionJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open version.json:" << file.errorString();
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "Invalid JSON format in version.json";
        return;
    }
    
    QJsonObject root = doc.object();
    QString versionId = root["id"].toString();
    
    qDebug() << QString("Version: %1").arg(versionId);
    
    // 1. Download client.jar
    QJsonObject downloads = root["downloads"].toObject();
    if (downloads.contains("client")) {
        QJsonObject client = downloads["client"].toObject();
        QString clientUrl = client["url"].toString();
        qint64 clientSize = client["size"].toVariant().toLongLong();
        
        // Create directory and path: ./minecraft/versions/{version}/{version}.jar
        QString versionDir = QString("minecraft/versions/%1").arg(versionId);
        QDir().mkpath(versionDir);
        QString jarPath = QString("%1/%2.jar").arg(versionDir).arg(versionId);
        
        // Check if file already exists
        QFileInfo fileInfo(jarPath);
        if (!fileInfo.exists() || fileInfo.size() != clientSize) {
            downloader->addDownload(QUrl(clientUrl), jarPath, 10);  // Higher priority
            qDebug() << QString("Added client.jar download: %1").arg(versionId);
        }
    }
    
    // 2. Download libraries
    QJsonArray libraries = root["libraries"].toArray();
    qDebug() << QString("Found %1 libraries").arg(libraries.size());
    
    QDir().mkpath("minecraft/libraries");
    
    int libCount = 0;
    for (const QJsonValue &libValue : libraries) {
        QJsonObject lib = libValue.toObject();
        
        // Check if library has rules (platform-specific)
        if (lib.contains("rules")) {
            // For simplicity, skip platform-specific libraries for now
            // In a full implementation, you would check the rules
            continue;
        }
        
        // Get download information
        QJsonObject downloads = lib["downloads"].toObject();
        if (!downloads.contains("artifact")) {
            continue;
        }
        
        QJsonObject artifact = downloads["artifact"].toObject();
        QString url = artifact["url"].toString();
        QString path = artifact["path"].toString();
        qint64 size = artifact["size"].toVariant().toLongLong();
        
        if (url.isEmpty() || path.isEmpty()) {
            continue;
        }
        
        // Create local path: ./minecraft/libraries/{path}
        QString localPath = QString("minecraft/libraries/%1").arg(path);
        
        // Ensure directory exists
        QFileInfo fileInfo(localPath);
        QString dirPath = fileInfo.absolutePath();
        QDir().mkpath(dirPath);
        
        // Check if file already exists
        if (fileInfo.exists() && fileInfo.size() == size) {
            continue;  // Skip already downloaded files
        }
        
        // Add download task
        downloader->addDownload(QUrl(url), localPath, 5);  // Medium priority
        libCount++;
    }
    
    qDebug() << QString("Added %1 new library download tasks").arg(libCount);
}
