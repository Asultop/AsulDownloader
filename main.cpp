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
#include <QElapsedTimer>
#include "AsulMultiDownloader.h"

void parseAndDownloadAssets(AsulMultiDownloader *downloader, const QString &assetsJsonPath, qint64 *totalBytes = nullptr);
void parseAndDownloadVersion(AsulMultiDownloader *versionDownloader, AsulMultiDownloader *libraryDownloader,
                             const QString &versionJsonPath, qint64 *totalBytes = nullptr);

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    qDebug() << "AsulDownloader - Minecraft Asset and Version Downloader";
    qDebug() << "========================================================";
    
    // Create downloader instances
    AsulMultiDownloader assetsDownloader;
    AsulMultiDownloader librariesDownloader;
    AsulMultiDownloader versionDownloader;
    
    // Configure downloader for assets (512 concurrent, same host limit)
    assetsDownloader.setMaxConcurrentDownloads(512);
    assetsDownloader.setMaxConnectionsPerHost(512);
    assetsDownloader.setLargeFileThreshold(1LL * 1024 * 1024);  // 1MB threshold for assets
    assetsDownloader.setSegmentCountForLargeFile(4);
    // Configure downloader for libraries (64 concurrent, same host limit)
    librariesDownloader.setMaxConcurrentDownloads(64);
    librariesDownloader.setMaxConnectionsPerHost(64);
    librariesDownloader.setLargeFileThreshold(5LL * 1024 * 1024);  // 5MB threshold for libraries
    librariesDownloader.setSegmentCountForLargeFile(4);
    // Configure downloader for version.jar (segment download with 10MB threshold)
    versionDownloader.setLargeFileThreshold(10LL * 1024 * 1024);  // 10MB threshold for version.jar
    versionDownloader.setSegmentCountForLargeFile(8);
    
    qDebug() << "Concurrency Config:";
    qDebug() << "  Assets Max Concurrent:" << assetsDownloader.maxConcurrentDownloads();
    qDebug() << "  Assets Max Conn/Host:" << assetsDownloader.maxConnectionsPerHost();
    qDebug() << "  Libraries Max Concurrent:" << librariesDownloader.maxConcurrentDownloads();
    qDebug() << "  Libraries Max Conn/Host:" << librariesDownloader.maxConnectionsPerHost();
    
    // Track completion
    int totalTasks = 0;
    int completedTasks = 0;
    int failedTasks = 0;
    qint64 totalBytes = 0;  // Total bytes to download
    QElapsedTimer downloadTimer;
    qint64 lastTotalDownloadedAssets = 0;
    qint64 lastTotalDownloadedLibraries = 0;
    qint64 lastTotalDownloadedVersion = 0;
    
    auto onDownloadFinished = [&](const QString &taskId, const QString &savePath) {
        Q_UNUSED(taskId);
        completedTasks++;
        if (completedTasks % 100 == 0 || completedTasks == totalTasks) {
            qDebug() << QString("[%1/%2] Completed: %3")
                        .arg(completedTasks)
                        .arg(totalTasks)
                        .arg(QFileInfo(savePath).fileName());
        }
    };
    
    auto onDownloadFailed = [&](const QString &taskId, const QString &errorString) {
        failedTasks++;
        qWarning() << QString("[FAILED] Task %1: %2").arg(taskId).arg(errorString);
    };
    
    QObject::connect(&assetsDownloader, &AsulMultiDownloader::downloadFinished, onDownloadFinished);
    QObject::connect(&librariesDownloader, &AsulMultiDownloader::downloadFinished, onDownloadFinished);
    QObject::connect(&versionDownloader, &AsulMultiDownloader::downloadFinished, onDownloadFinished);
    
    QObject::connect(&assetsDownloader, &AsulMultiDownloader::downloadFailed, onDownloadFailed);
    QObject::connect(&librariesDownloader, &AsulMultiDownloader::downloadFailed, onDownloadFailed);
    QObject::connect(&versionDownloader, &AsulMultiDownloader::downloadFailed, onDownloadFailed);
    
    // Add statistics reporting every 500ms (combined)
    QTimer *statsTimer = new QTimer(&a);
    QObject::connect(statsTimer, &QTimer::timeout, [&]() {
        DownloadStatistics assetsStats = assetsDownloader.getStatistics();
        DownloadStatistics librariesStats = librariesDownloader.getStatistics();
        DownloadStatistics versionStats = versionDownloader.getStatistics();
        
        // Calculate downloaded file count (completed + failed)
        int downloadedFiles = completedTasks + failedTasks;
        
        // Calculate downloading file count (active downloads)
        int downloadingFiles = assetsStats.activeDownloads + librariesStats.activeDownloads + versionStats.activeDownloads;
        
        // Get downloaded bytes
        lastTotalDownloadedAssets = assetsStats.totalDownloaded;
        lastTotalDownloadedLibraries = librariesStats.totalDownloaded;
        lastTotalDownloadedVersion = versionStats.totalDownloaded;
        qint64 downloadedBytes = lastTotalDownloadedAssets + lastTotalDownloadedLibraries + lastTotalDownloadedVersion;
        
        // Calculate speed in MB/s
        qint64 totalSpeedBytes = assetsStats.totalDownloadSpeed + librariesStats.totalDownloadSpeed + versionStats.totalDownloadSpeed;
        double speedMBps = totalSpeedBytes / (1024.0 * 1024.0);
        
        qDebug() << QString("[PROGRESS] %1 | %2 | %3 | %4 MB | %5 MB | Speed: %6 MB/s")
                    .arg(downloadedFiles)
                    .arg(downloadingFiles)
                    .arg(totalTasks)
                    .arg(downloadedBytes / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(speedMBps, 0, 'f', 2);
    });
    
    bool assetsFinished = false;
    bool librariesFinished = false;
    bool versionFinished = false;
    
    auto tryFinishAll = [&]() {
        if (!assetsFinished || !librariesFinished || !versionFinished) {
            return;
        }
        statsTimer->stop();
        const qint64 elapsedMs = downloadTimer.elapsed();
        const double elapsedSec = elapsedMs > 0 ? (elapsedMs / 1000.0) : 0.0;
        const qint64 totalDownloadedForAverage = (lastTotalDownloadedAssets + lastTotalDownloadedLibraries + lastTotalDownloadedVersion) > 0
            ? (lastTotalDownloadedAssets + lastTotalDownloadedLibraries + lastTotalDownloadedVersion)
            : totalBytes;
        const double averageSpeedMBps = elapsedSec > 0.0
            ? (totalDownloadedForAverage / (1024.0 * 1024.0)) / elapsedSec
            : 0.0;
        qDebug() << "\n========================================================";
        qDebug() << "All downloads finished!";
        qDebug() << QString("Completed: %1, Failed: %2, Total: %3")
                    .arg(completedTasks).arg(failedTasks).arg(totalTasks);
        qDebug() << QString("Average speed: %1 MB/s (Total %2 MB, Time %3 s)")
                    .arg(averageSpeedMBps, 0, 'f', 2)
                    .arg(totalDownloadedForAverage / (1024.0 * 1024.0), 0, 'f', 2)
                    .arg(elapsedSec, 0, 'f', 2);
        qDebug() << "========================================================";
        QCoreApplication::quit();
    };
    
    QObject::connect(&assetsDownloader, &AsulMultiDownloader::allDownloadsFinished,
                     [&]() {
        assetsFinished = true;
        tryFinishAll();
    });
    QObject::connect(&librariesDownloader, &AsulMultiDownloader::allDownloadsFinished,
                     [&]() {
        librariesFinished = true;
        tryFinishAll();
    });
    QObject::connect(&versionDownloader, &AsulMultiDownloader::allDownloadsFinished,
                     [&]() {
        versionFinished = true;
        tryFinishAll();
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
    parseAndDownloadAssets(&assetsDownloader, "assets.json", &totalBytes);
    
    // Parse and download version files
    qDebug() << "\nParsing version.json...";
    QFileInfo versionFile("version.json");
    if (!versionFile.exists()) {
        qWarning() << "ERROR: version.json not found!";
        qWarning() << "Please place version.json in the same directory as the executable.";
        qWarning() << "Current directory:" << QDir::currentPath();
        return 1;
    }
    parseAndDownloadVersion(&versionDownloader, &librariesDownloader, "version.json", &totalBytes);
    
    // Get total task count
    int assetsTasks = assetsDownloader.getAllTaskIds().count();
    int librariesTasks = librariesDownloader.getAllTaskIds().count();
    int versionTasks = versionDownloader.getAllTaskIds().count();
    totalTasks = assetsTasks + librariesTasks + versionTasks;
    qDebug() << QString("\nTotal tasks added: %1").arg(totalTasks);
    qDebug() << QString("Total size: %1 MB").arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 2);
    
    if (totalTasks == 0) {
        qDebug() << "All files are already downloaded. Nothing to do.";
        qDebug() << "========================================================";
        return 0;
    }
    
    if (assetsTasks == 0) {
        assetsFinished = true;
    }
    if (librariesTasks == 0) {
        librariesFinished = true;
    }
    if (versionTasks == 0) {
        versionFinished = true;
    }
    
    qDebug() << "Starting downloads...\n";
    downloadTimer.start();
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

void parseAndDownloadVersion(AsulMultiDownloader *versionDownloader, AsulMultiDownloader *libraryDownloader,
                             const QString &versionJsonPath, qint64 *totalBytes)
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
            versionDownloader->addDownload(QUrl(clientUrl), jarPath, 10, clientSize);  // Higher priority
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
        libraryDownloader->addDownload(QUrl(url), localPath, 5, size);  // Medium priority
        libCount++;
        
        // Add to total bytes
        if (totalBytes && size > 0) {
            *totalBytes += size;
        }
    }
    
    qDebug() << QString("Added %1 new library download tasks").arg(libCount);
}
