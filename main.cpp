/**
 * AsulDownloader - Minecraft Asset and Version Downloader
 * 
 * This application parses Minecraft's assets.json and version.json files
 * and downloads all required files using the high-performance AsulMultiDownloader.
 * 
 * Features:
 * - Parallel download of thousands of asset files
 * - Download client.jar and rename to {version}.jar
 * - Download all library files with proper directory structure
 * - Skip already downloaded files (resume capability)
 * - Real-time progress statistics
 * 
 * Directory structure:
 * - Assets: ./minecraft/assets/objects/{first 2 chars of hash}/{hash}
 * - Client: ./minecraft/versions/{version}/{version}.jar
 * - Libraries: ./minecraft/libraries/{path from version.json}
 */

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QUrl>
#include <QTimer>
#include "AsulMultiDownloader.h"

void parseAndDownloadAssets(AsulMultiDownloader *downloader, const QString &assetsJsonPath, qint64 *totalBytes = nullptr);
void parseAndDownloadVersion(AsulMultiDownloader *downloader, const QString &versionJsonPath, qint64 *totalBytes = nullptr);

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    qDebug() << "AsulDownloader - Minecraft Asset and Version Downloader";
    qDebug() << "========================================================";
    
    // Create downloader instance
    AsulMultiDownloader downloader;
    
    // Configure downloader for optimal performance
    // 优化：极大幅增加并发数，针对小文件下载，突破TCP/HTTP延迟瓶颈
    downloader.setMaxConcurrentDownloads(512);  // 512并发
    downloader.setMaxConnectionsPerHost(512);   // 允许针对同一CDN建立大量连接
    downloader.setLargeFileThreshold(10 * 1024 * 1024);  // 10MB threshold
    downloader.setSegmentCountForLargeFile(8);  // 8 segments for large files
    
    qDebug() << "Concurrency Config:";
    qDebug() << "  Max Concurrent:" << downloader.maxConcurrentDownloads();
    qDebug() << "  Max Conn/Host:" << downloader.maxConnectionsPerHost();
    
    // Track completion
    int totalTasks = 0;
    int completedTasks = 0;
    int failedTasks = 0;
    qint64 totalBytes = 0;  // Total bytes to download
    
    // Connect signals to track progress
    QObject::connect(&downloader, &AsulMultiDownloader::downloadFinished,
                     [&](const QString &taskId, const QString &savePath) {
        completedTasks++;
        if (completedTasks % 100 == 0 || completedTasks == totalTasks) {
            qDebug() << QString("[%1/%2] Completed: %3")
                        .arg(completedTasks)
                        .arg(totalTasks)
                        .arg(QFileInfo(savePath).fileName());
        }
    });
    
    QObject::connect(&downloader, &AsulMultiDownloader::downloadFailed,
                     [&](const QString &taskId, const QString &errorString) {
        failedTasks++;
        qWarning() << QString("[FAILED] Task %1: %2").arg(taskId).arg(errorString);
    });
    
    // Add statistics reporting every 500ms
    QTimer *statsTimer = new QTimer(&a);
    QObject::connect(statsTimer, &QTimer::timeout, [&]() {
        DownloadStatistics stats = downloader.getStatistics();
        
        // Calculate downloaded file count (completed + failed)
        int downloadedFiles = completedTasks + failedTasks;
        
        // Calculate downloading file count (active downloads)
        int downloadingFiles = stats.activeDownloads;
        
        // Get downloaded bytes
        qint64 downloadedBytes = stats.totalDownloaded;
        
        // Calculate speed in MB/s
        double speedMBps = stats.totalDownloadSpeed / (1024.0 * 1024.0);
        
        qDebug() << QString("[PROGRESS] %1 | %2 | %3 | %4 MB | %5 MB | Speed: %6 MB/s")
                    .arg(downloadedFiles)
                    .arg(downloadingFiles)
                    .arg(totalTasks)
                    .arg(downloadedBytes / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(speedMBps, 0, 'f', 2);
    });
    
    QObject::connect(&downloader, &AsulMultiDownloader::allDownloadsFinished,
                     [&, statsTimer]() {
        statsTimer->stop();
        qDebug() << "\n========================================================";
        qDebug() << "All downloads finished!";
        qDebug() << QString("Completed: %1, Failed: %2, Total: %3")
                    .arg(completedTasks).arg(failedTasks).arg(totalTasks);
        qDebug() << "========================================================";
        QCoreApplication::quit();
    });
    
    // Parse and download assets
    qDebug() << "\nParsing assets.json...";
    QFileInfo assetsFile("assets.json");
    if (!assetsFile.exists()) {
        qWarning() << "ERROR: assets.json not found!";
        qWarning() << "Please place assets.json in the same directory as the executable.";
        qWarning() << "Current directory:" << QDir::currentPath();
        return 1;
    }
    parseAndDownloadAssets(&downloader, "assets.json", &totalBytes);
    
    // Parse and download version files
    qDebug() << "\nParsing version.json...";
    QFileInfo versionFile("version.json");
    if (!versionFile.exists()) {
        qWarning() << "ERROR: version.json not found!";
        qWarning() << "Please place version.json in the same directory as the executable.";
        qWarning() << "Current directory:" << QDir::currentPath();
        return 1;
    }
    parseAndDownloadVersion(&downloader, "version.json", &totalBytes);
    
    // Get total task count
    totalTasks = downloader.getAllTaskIds().count();
    qDebug() << QString("\nTotal tasks added: %1").arg(totalTasks);
    qDebug() << QString("Total size: %1 MB").arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 2);
    
    if (totalTasks == 0) {
        qDebug() << "All files are already downloaded. Nothing to do.";
        qDebug() << "========================================================";
        return 0;
    }
    
    qDebug() << "Starting downloads...\n";
    statsTimer->start(500);  // Start stats timer (report every 500ms)

    return a.exec();
}

void parseAndDownloadAssets(AsulMultiDownloader *downloader, const QString &assetsJsonPath, qint64 *totalBytes)
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
        
        // Add download task with low priority (0) for assets
        // Priority: 0 = low (assets), 5 = medium (libraries), 10 = high (client.jar)
        downloader->addDownload(QUrl(url), localPath, 0, size);
        count++;
        
        // Add to total bytes
        if (totalBytes && size > 0) {
            *totalBytes += size;
        }
    }
    
    qDebug() << QString("Added %1 new asset download tasks").arg(count);
}

void parseAndDownloadVersion(AsulMultiDownloader *downloader, const QString &versionJsonPath, qint64 *totalBytes)
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
            downloader->addDownload(QUrl(clientUrl), jarPath, 10, clientSize);  // Higher priority
            qDebug() << QString("Added client.jar download: %1").arg(versionId);
            
            // Add to total bytes
            if (totalBytes && clientSize > 0) {
                *totalBytes += clientSize;
            }
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
            // Skip platform-specific libraries for cross-platform compatibility
            // In a full implementation, you would parse the rules to determine
            // if the library is needed for the current platform (Windows/Linux/macOS)
            // TODO: Implement rule checking for platform-specific libraries
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
        downloader->addDownload(QUrl(url), localPath, 5, size);  // Medium priority
        libCount++;
        
        // Add to total bytes
        if (totalBytes && size > 0) {
            *totalBytes += size;
        }
    }
    
    qDebug() << QString("Added %1 new library download tasks").arg(libCount);
}
