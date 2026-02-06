#include "AsulMultiDownloader.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>
#include <atomic>

// ==================== AsulMultiDownloader 实现 ====================

  AsulMultiDownloader::AsulMultiDownloader(QObject *parent)
    : QObject(parent)
    , m_maxConcurrentDownloads(512)
    , m_largeFileThreshold(10 * 1024 * 1024)
    , m_segmentCount(8)
    , m_maxConnectionsPerHost(512)
    , m_downloadTimeout(30000)
    , m_autoRetry(true)
    , m_maxRetryCount(3)
    , m_stallTimeout(15000)  // 默认15秒无数据传输则视为停滞
    , m_activeDownloads(0)
    , m_taskIdCounter(0)
    , m_speedMonitoringEnabled(true)
    , m_speedThreshold(256 * 1024)
    , m_lastSpeedCheck(0)
    , m_lastBytesDownloaded(0)
    , m_monitorLastTime(0)
    , m_monitorLastBytes(0)
    , m_allFinishedEmitted(false)
    , m_networkManagerPoolSize(32)  // 优化：使用32个网络管理器，分散负载并增加总连接数限制
{
    // 初始化网络管理器池
    for (int i = 0; i < m_networkManagerPoolSize; ++i) {
        QNetworkAccessManager *manager = new QNetworkAccessManager(this);
        m_networkManagers.append(manager);
    }
    
    m_statisticsTimer = new QTimer(this);
    connect(m_statisticsTimer, &QTimer::timeout, this, &AsulMultiDownloader::onUpdateStatistics);
    m_statisticsTimer->start(1000);  // 每秒更新一次统计信息
    
    // 启动监控线程，用于动态线程调度（参考PCL）
    // 优化：降低监控频率从20ms到1000ms，减少系统负载
    m_monitorTimer = new QTimer(this);
    connect(m_monitorTimer, &QTimer::timeout, this, &AsulMultiDownloader::onMonitorDownloads);
    m_monitorTimer->start(1000);  // 每秒检查一次，大幅降低频率
    
    // 初始化域名策略（参考PCL对特定域名的优化）
    m_noMultiThreadHosts << "bmclapi" << "github.com" << "modrinth.com" 
                         << "optifine.net" << "curseforge.com";
}

AsulMultiDownloader::~AsulMultiDownloader()
{
    cancelAll();
}

// ==================== 配置接口实现 ====================

void AsulMultiDownloader::setMaxConcurrentDownloads(int count)
{
    QMutexLocker locker(&m_mutex);
    m_maxConcurrentDownloads = qMax(1, count);
    processQueue();
}

int AsulMultiDownloader::maxConcurrentDownloads() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxConcurrentDownloads;
}

void AsulMultiDownloader::setLargeFileThreshold(qint64 bytes)
{
    QMutexLocker locker(&m_mutex);
    m_largeFileThreshold = qMax(qint64(0), bytes);
}

qint64 AsulMultiDownloader::largeFileThreshold() const
{
    QMutexLocker locker(&m_mutex);
    return m_largeFileThreshold;
}

void AsulMultiDownloader::setSegmentCountForLargeFile(int count)
{
    QMutexLocker locker(&m_mutex);
    m_segmentCount = qMax(1, count);
}

int AsulMultiDownloader::segmentCountForLargeFile() const
{
    QMutexLocker locker(&m_mutex);
    return m_segmentCount;
}

void AsulMultiDownloader::setMaxConnectionsPerHost(int count)
{
    QMutexLocker locker(&m_mutex);
    m_maxConnectionsPerHost = qMax(1, count);
}

int AsulMultiDownloader::maxConnectionsPerHost() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxConnectionsPerHost;
}

void AsulMultiDownloader::setDownloadTimeout(int msecs)
{
    QMutexLocker locker(&m_mutex);
    m_downloadTimeout = qMax(1000, msecs);
}

int AsulMultiDownloader::downloadTimeout() const
{
    QMutexLocker locker(&m_mutex);
    return m_downloadTimeout;
}

void AsulMultiDownloader::setAutoRetry(bool enable)
{
    QMutexLocker locker(&m_mutex);
    m_autoRetry = enable;
}

bool AsulMultiDownloader::autoRetry() const
{
    QMutexLocker locker(&m_mutex);
    return m_autoRetry;
}

void AsulMultiDownloader::setMaxRetryCount(int count)
{
    QMutexLocker locker(&m_mutex);
    m_maxRetryCount = qMax(0, count);
}

int AsulMultiDownloader::maxRetryCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxRetryCount;
}

void AsulMultiDownloader::setStallTimeout(int msecs)
{
    QMutexLocker locker(&m_mutex);
    m_stallTimeout = qMax(1000, msecs);  // 最小1秒
}

int AsulMultiDownloader::stallTimeout() const
{
    QMutexLocker locker(&m_mutex);
    return m_stallTimeout;
}

// ==================== PCL优化新增接口实现 ====================

void AsulMultiDownloader::setSpeedThreshold(qint64 bytesPerSecond)
{
    QMutexLocker locker(&m_mutex);
    m_speedThreshold = qMax(qint64(0), bytesPerSecond);
}

qint64 AsulMultiDownloader::speedThreshold() const
{
    QMutexLocker locker(&m_mutex);
    return m_speedThreshold;
}

void AsulMultiDownloader::setSpeedMonitoringEnabled(bool enable)
{
    QMutexLocker locker(&m_mutex);
    m_speedMonitoringEnabled = enable;
}

bool AsulMultiDownloader::speedMonitoringEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_speedMonitoringEnabled;
}

void AsulMultiDownloader::addNoMultiThreadHost(const QString &host)
{
    QMutexLocker locker(&m_mutex);
    if (!m_noMultiThreadHosts.contains(host)) {
        m_noMultiThreadHosts.append(host);
    }
}

void AsulMultiDownloader::removeNoMultiThreadHost(const QString &host)
{
    QMutexLocker locker(&m_mutex);
    m_noMultiThreadHosts.removeAll(host);
}

void AsulMultiDownloader::clearNoMultiThreadHosts()
{
    QMutexLocker locker(&m_mutex);
    m_noMultiThreadHosts.clear();
}

QStringList AsulMultiDownloader::noMultiThreadHosts() const
{
    QMutexLocker locker(&m_mutex);
    return m_noMultiThreadHosts;
}

// ==================== 下载控制接口实现 ====================

QString AsulMultiDownloader::addDownload(const QUrl &url, const QString &savePath, int priority)
{
    QMutexLocker locker(&m_mutex);
    
    QString taskId = generateTaskId();
    
    // 创建下载任务
    auto task = std::make_shared<DownloadTask>(taskId, url, savePath, priority, this);
    task->setTimeout(m_downloadTimeout);
    
    // 设置分段数
    task->setSegmentCount(m_segmentCount);
    
    // 连接信号
    connect(task.get(), &DownloadTask::started, this, &AsulMultiDownloader::downloadStarted);
    connect(task.get(), &DownloadTask::progress, this, &AsulMultiDownloader::onTaskProgress);
    connect(task.get(), &DownloadTask::finished, this, &AsulMultiDownloader::onTaskFinished);
    connect(task.get(), &DownloadTask::failed, this, &AsulMultiDownloader::onTaskFailed);
    connect(task.get(), &DownloadTask::paused, this, &AsulMultiDownloader::downloadPaused);
    connect(task.get(), &DownloadTask::resumed, this, &AsulMultiDownloader::downloadResumed);
    
    m_tasks[taskId] = task;
    m_taskStatus[taskId] = DownloadStatus::Queued;
    m_taskRetryCount[taskId] = 0;
    m_taskQueue.enqueue(taskId);
    
    emit downloadAdded(taskId, url);
    
    // 尝试处理队列
    processQueue();
    
    return taskId;
}

QStringList AsulMultiDownloader::addDownloads(const QList<QUrl> &urls, const QStringList &savePaths, int priority)
{
    QStringList taskIds;
    
    int count = qMin(urls.size(), savePaths.size());
    for (int i = 0; i < count; ++i) {
        QString taskId = addDownload(urls[i], savePaths[i], priority);
        taskIds.append(taskId);
    }
    
    return taskIds;
}

void AsulMultiDownloader::pauseDownload(const QString &taskId)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    auto task = m_tasks[taskId];
    DownloadStatus status = m_taskStatus[taskId];
    
    if (status == DownloadStatus::Downloading) {
        task->pause();
        m_taskStatus[taskId] = DownloadStatus::Paused;
        updateHostConnections(task->url().host(), -1);
        m_activeDownloads--;
        processQueue();
    } else if (status == DownloadStatus::Queued) {
        m_taskStatus[taskId] = DownloadStatus::Paused;
        m_taskQueue.removeOne(taskId);
    }
}

void AsulMultiDownloader::resumeDownload(const QString &taskId)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    DownloadStatus status = m_taskStatus[taskId];
    
    if (status == DownloadStatus::Paused) {
        m_taskStatus[taskId] = DownloadStatus::Queued;
        m_taskQueue.enqueue(taskId);
        processQueue();
    }
}

void AsulMultiDownloader::cancelDownload(const QString &taskId)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    auto task = m_tasks[taskId];
    DownloadStatus status = m_taskStatus[taskId];
    
    if (status == DownloadStatus::Downloading) {
        task->cancel();
        m_taskStatus[taskId] = DownloadStatus::Canceled;
        updateHostConnections(task->url().host(), -1);
        m_activeDownloads--;
        processQueue();
    } else if (status == DownloadStatus::Queued) {
        m_taskStatus[taskId] = DownloadStatus::Canceled;
        m_taskQueue.removeOne(taskId);
    } else {
        m_taskStatus[taskId] = DownloadStatus::Canceled;
    }
    
    emit downloadCanceled(taskId);
}

void AsulMultiDownloader::pauseAll()
{
    QMutexLocker locker(&m_mutex);
    
    QStringList taskIds = m_tasks.keys();
    for (const QString &taskId : taskIds) {
        locker.unlock();
        pauseDownload(taskId);
        locker.relock();
    }
}

void AsulMultiDownloader::resumeAll()
{
    QMutexLocker locker(&m_mutex);
    
    QStringList taskIds = m_tasks.keys();
    for (const QString &taskId : taskIds) {
        locker.unlock();
        resumeDownload(taskId);
        locker.relock();
    }
}

void AsulMultiDownloader::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    
    QStringList taskIds = m_tasks.keys();
    for (const QString &taskId : taskIds) {
        locker.unlock();
        cancelDownload(taskId);
        locker.relock();
    }
}

void AsulMultiDownloader::clearFinishedTasks()
{
    QMutexLocker locker(&m_mutex);
    
    QStringList toRemove;
    for (auto it = m_taskStatus.begin(); it != m_taskStatus.end(); ++it) {
        if (it.value() == DownloadStatus::Completed || 
            it.value() == DownloadStatus::Failed ||
            it.value() == DownloadStatus::Canceled) {
            toRemove.append(it.key());
        }
    }
    
    for (const QString &taskId : toRemove) {
        m_tasks.remove(taskId);
        m_taskStatus.remove(taskId);
        m_taskRetryCount.remove(taskId);
    }
}

// ==================== 查询接口实现 ====================

DownloadInfo AsulMultiDownloader::getDownloadInfo(const QString &taskId) const
{
    QMutexLocker locker(&m_mutex);
    
    DownloadInfo info;
    
    if (!m_tasks.contains(taskId)) {
        return info;
    }
    
    auto task = m_tasks[taskId];
    info.taskId = taskId;
    info.url = task->url();
    info.savePath = task->savePath();
    info.fileSize = task->fileSize();
    info.downloadedSize = task->downloadedSize();
    info.priority = task->priority();
    info.supportRange = task->supportRange();
    info.segmentCount = task->segmentCount();
    info.errorString = task->errorString();
    
    return info;
}

DownloadStatus AsulMultiDownloader::getDownloadStatus(const QString &taskId) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_taskStatus.contains(taskId)) {
        return DownloadStatus::Failed;
    }
    
    return m_taskStatus[taskId];
}

double AsulMultiDownloader::getDownloadProgress(const QString &taskId) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_tasks.contains(taskId)) {
        return 0.0;
    }
    
    auto task = m_tasks[taskId];
    qint64 fileSize = task->fileSize();
    
    if (fileSize <= 0) {
        return 0.0;
    }
    
    return (task->downloadedSize() * 100.0) / fileSize;
}

qint64 AsulMultiDownloader::getDownloadSpeed(const QString &taskId) const
{
    // 这个方法需要额外的速度跟踪机制，暂时返回0
    // 在实际应用中，可以维护一个时间窗口内的下载量来计算速度
    Q_UNUSED(taskId);
    return 0;
}

QStringList AsulMultiDownloader::getAllTaskIds() const
{
    QMutexLocker locker(&m_mutex);
    return m_tasks.keys();
}

DownloadStatistics AsulMultiDownloader::getStatistics() const
{
    QMutexLocker locker(&m_mutex);
    return m_statistics;
}

// ==================== 私有槽函数实现 ====================

void AsulMultiDownloader::onTaskFinished(const QString &taskId)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    auto task = m_tasks[taskId];
    m_taskStatus[taskId] = DownloadStatus::Completed;
    updateHostConnections(task->url().host(), -1);
    m_activeDownloads--;
    m_statistics.completedTasks++;
    
    emit downloadFinished(taskId, task->savePath());
    
    processQueue();
    
    // 检查是否所有任务都完成了
    checkAndEmitAllFinished();
}

void AsulMultiDownloader::onTaskFailed(const QString &taskId, const QString &error)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    auto task = m_tasks[taskId];
    updateHostConnections(task->url().host(), -1);
    m_activeDownloads--;
    
    // 检查是否需要重试
    if (m_autoRetry && m_taskRetryCount[taskId] < m_maxRetryCount) {
        m_taskRetryCount[taskId]++;
        m_taskStatus[taskId] = DownloadStatus::Queued;
        m_taskQueue.enqueue(taskId);
        
        emit downloadRetrying(taskId, m_taskRetryCount[taskId]);
        
        processQueue();
    } else {
        m_taskStatus[taskId] = DownloadStatus::Failed;
        m_statistics.failedTasks++;
        
        emit downloadFailed(taskId, error);
        
        processQueue();
        
        // 检查是否所有任务都完成了（包括失败的任务）
        checkAndEmitAllFinished();
    }
}

void AsulMultiDownloader::onTaskProgress(const QString &taskId, qint64 received, qint64 total)
{
    emit downloadProgress(taskId, received, total);
}

void AsulMultiDownloader::onUpdateStatistics()
{
    QMutexLocker locker(&m_mutex);
    
    m_statistics.activeDownloads = m_activeDownloads;
    m_statistics.queuedDownloads = m_taskQueue.size();
    
    // 计算总下载量
    qint64 totalDownloaded = 0;
    for (auto task : m_tasks) {
        totalDownloaded += task->downloadedSize();
    }
    m_statistics.totalDownloaded = totalDownloaded;
    
    // 计算下载速度（使用移动平均）
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (m_lastSpeedCheck > 0) {
        qint64 timeDiff = currentTime - m_lastSpeedCheck;
        if (timeDiff >= 1000) {  // 至少1秒
            qint64 bytesDiff = totalDownloaded - m_lastBytesDownloaded;
            // 计算速度（字节/秒）
            m_statistics.totalDownloadSpeed = (bytesDiff * 1000) / timeDiff;
            
            // 限制最大速度显示（避免显示不合理的高速度）
            // 假设最大速度为1GB/s
            if (m_statistics.totalDownloadSpeed > 1024 * 1024 * 1024) {
                m_statistics.totalDownloadSpeed = 0;  // 重置异常值
            }
            
            m_lastSpeedCheck = currentTime;
            m_lastBytesDownloaded = totalDownloaded;
        }
    } else {
        m_lastSpeedCheck = currentTime;
        m_lastBytesDownloaded = totalDownloaded;
        m_statistics.totalDownloadSpeed = 0;
    }
    
    emit statisticsChanged(m_statistics);
}

// ==================== 私有方法实现 ====================

void AsulMultiDownloader::processQueue()
{
    // 注意：调用此方法时应已持有锁
    
    while (!m_taskQueue.isEmpty() && m_activeDownloads < m_maxConcurrentDownloads) {
        QString taskId = m_taskQueue.dequeue();
        
        if (!m_tasks.contains(taskId)) {
            continue;
        }
        
        auto task = m_tasks[taskId];
        QString host = task->url().host();
        
        // 检查是否可以启动下载（考虑Host连接限制）
        if (!canStartDownload(host)) {
            // 重新入队，等待下次处理
            m_taskQueue.enqueue(taskId);
            break;
        }
        
        startDownloadTask(task);
    }
}

void AsulMultiDownloader::startDownloadTask(std::shared_ptr<DownloadTask> task)
{
    QString taskId = task->taskId();
    QString host = task->url().host();
    
    m_taskStatus[taskId] = DownloadStatus::Downloading;
    updateHostConnections(host, 1);
    m_activeDownloads++;
    
    // 启动任务
    task->start();
}

QString AsulMultiDownloader::generateTaskId()
{
    m_taskIdCounter++;
    return QString("task_%1_%2")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(m_taskIdCounter);
}

void AsulMultiDownloader::updateHostConnections(const QString &host, int delta)
{
    if (!m_hostConnections.contains(host)) {
        m_hostConnections[host] = 0;
    }
    
    m_hostConnections[host] += delta;
    
    if (m_hostConnections[host] <= 0) {
        m_hostConnections.remove(host);
    }
}

int AsulMultiDownloader::getHostConnections(const QString &host) const
{
    return m_hostConnections.value(host, 0);
}

bool AsulMultiDownloader::canStartDownload(const QString &host) const
{
    return getHostConnections(host) < m_maxConnectionsPerHost;
}

// ==================== DownloadTask 实现 ====================

DownloadTask::DownloadTask(const QString &taskId, const QUrl &url, 
                         const QString &savePath, int priority, QObject *parent)
    : QObject(parent)
    , m_taskId(taskId)
    , m_url(url)
    , m_savePath(savePath)
    , m_priority(priority)
    , m_timeout(30000)
    , m_stallTimeout(15000)
    , m_fileSize(-1)
    , m_downloadedSize(0)
    , m_lastProgressBytes(0)
    , m_lastProgressTime(0)
    , m_supportRange(false)
    , m_segmentCount(1)
    , m_networkManager(nullptr)
    , m_reply(nullptr)
    , m_file(nullptr)
    , m_ownsNetworkManager(false)
    , m_stallTimer(nullptr)
    , m_completedSegments(0)
    , m_isPaused(false)
    , m_isCanceled(false)
{
    // 从父对象（AsulMultiDownloader）获取共享的网络管理器
    AsulMultiDownloader *downloader = qobject_cast<AsulMultiDownloader*>(parent);
    if (downloader) {
        m_networkManager = downloader->getNetworkManager();
        m_ownsNetworkManager = false;
        m_stallTimeout = downloader->stallTimeout();  // 获取停滞超时配置
    } else {
        // 如果无法获取，创建自己的（兜底方案）
        m_networkManager = new QNetworkAccessManager(this);
        m_ownsNetworkManager = true;
    }
    
    // 创建停滞检查定时器
    m_stallTimer = new QTimer(this);
    m_stallTimer->setSingleShot(false);
    connect(m_stallTimer, &QTimer::timeout, this, &DownloadTask::onStallCheck);
}

DownloadTask::~DownloadTask()
{
    cancel();
    
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    
    // 注意：不需要显式释放网络管理器
    // - 如果是从池中借用的（m_ownsNetworkManager == false），池会一直持有这些管理器
    // - 如果是自己创建的（m_ownsNetworkManager == true），Qt的父子关系会自动删除
}

void DownloadTask::start()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isPaused || m_isCanceled) {
        return;
    }
    
    // 确保保存目录存在
    QFileInfo fileInfo(m_savePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    emit started(m_taskId);
    
    // 首先发送HEAD请求获取文件大小和是否支持Range
    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeout);
    
    m_reply = m_networkManager->head(request);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadTask::onHeadFinished);
    connect(m_reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &DownloadTask::onDownloadError);
}

void DownloadTask::pause()
{
    QMutexLocker locker(&m_mutex);
    
    m_isPaused = true;
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    
    // 取消所有分段下载
    for (auto segment : m_segments) {
        segment->cancel();
        segment->deleteLater();
    }
    m_segments.clear();
    
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    
    emit paused(m_taskId);
}

void DownloadTask::resume()
{
    QMutexLocker locker(&m_mutex);
    
    m_isPaused = false;
    
    // 重新开始下载
    locker.unlock();
    start();
}

void DownloadTask::cancel()
{
    QMutexLocker locker(&m_mutex);
    
    m_isCanceled = true;
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    
    // 取消所有分段下载
    for (auto segment : m_segments) {
        segment->cancel();
        segment->deleteLater();
    }
    m_segments.clear();
    
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void DownloadTask::onHeadFinished()
{
    if (!m_reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    if (m_reply->error() != QNetworkReply::NoError) {
        m_errorString = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        locker.unlock();
        emit failed(m_taskId, m_errorString);
        return;
    }
    
    // 获取文件大小
    m_fileSize = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    
    // 检查是否支持Range
    QString acceptRanges = m_reply->rawHeader("Accept-Ranges");
    m_supportRange = (acceptRanges.toLower() == "bytes");
    
    m_reply->deleteLater();
    m_reply = nullptr;
    
    // 根据文件大小决定下载策略
    AsulMultiDownloader *downloader = qobject_cast<AsulMultiDownloader*>(parent());
    
    // PCL优化：检查域名策略
    bool disableMultiThread = downloader && downloader->shouldDisableMultiThread(m_url);
    
    if (downloader && m_fileSize > downloader->largeFileThreshold() && m_supportRange && !disableMultiThread) {
        // 大文件，使用分段下载（除非域名在禁用列表中）
        locker.unlock();
        startSegmentedDownload();
    } else {
        // 小文件、不支持Range或域名禁用多线程，使用单线程下载
        m_segmentCount = 1;
        locker.unlock();
        startSingleDownload();
    }
}

void DownloadTask::startSingleDownload()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isPaused || m_isCanceled) {
        return;
    }
    
    // 初始化停滞检测
    m_lastProgressBytes = 0;
    m_lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    
    // 打开文件
    m_file = new QFile(m_savePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_errorString = QString("Cannot open file: %1").arg(m_savePath);
        delete m_file;
        m_file = nullptr;
        locker.unlock();
        emit failed(m_taskId, m_errorString);
        return;
    }
    
    // 开始下载
    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeout);
    
    m_reply = m_networkManager->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &DownloadTask::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadTask::onDownloadFinished);
    connect(m_reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &DownloadTask::onDownloadError);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_reply) {
            m_file->write(m_reply->readAll());
        }
    });
    
    // 启动停滞检查定时器（每5秒检查一次）
    if (m_stallTimer) {
        m_stallTimer->start(5000);
    }
}

void DownloadTask::startSegmentedDownload()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isPaused || m_isCanceled) {
        return;
    }
    
    if (m_fileSize <= 0) {
        // 文件大小未知，回退到单线程下载
        locker.unlock();
        startSingleDownload();
        return;
    }
    
    // 计算每个分段的大小
    qint64 segmentSize = m_fileSize / m_segmentCount;
    m_segmentProgress.resize(m_segmentCount);
    m_segmentProgress.fill(0);
    
    // 创建分段下载器
    for (int i = 0; i < m_segmentCount; ++i) {
        qint64 start = i * segmentSize;
        qint64 end = (i == m_segmentCount - 1) ? m_fileSize - 1 : (start + segmentSize - 1);
        
        QString segmentPath = m_savePath + QString(".part%1").arg(i);
        
        auto segment = new SegmentDownloader(i, m_url, segmentPath, start, end, m_timeout, m_stallTimeout, this);
        connect(segment, &SegmentDownloader::finished, this, &DownloadTask::onSegmentFinished);
        connect(segment, &SegmentDownloader::error, this, &DownloadTask::onSegmentError);
        connect(segment, &SegmentDownloader::progress, this, &DownloadTask::onSegmentProgress);
        
        m_segments.append(segment);
    }
    
    // 初始化停滞检测
    m_lastProgressBytes = 0;
    m_lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    
    // 启动所有分段下载
    for (auto segment : m_segments) {
        segment->start();
    }
    
    // 启动停滞检查定时器（每5秒检查一次）
    if (m_stallTimer) {
        m_stallTimer->start(5000);
    }
}

void DownloadTask::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    QMutexLocker locker(&m_mutex);
    m_downloadedSize = bytesReceived;
    
    // 更新停滞检测的进度信息
    m_lastProgressBytes = bytesReceived;
    m_lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    
    if (bytesTotal > 0) {
        m_fileSize = bytesTotal;
    }
    
    locker.unlock();
    emit progress(m_taskId, bytesReceived, bytesTotal);
}

void DownloadTask::onDownloadFinished()
{
    if (!m_reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    if (m_reply->error() != QNetworkReply::NoError) {
        m_errorString = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        
        if (m_file) {
            m_file->close();
            delete m_file;
            m_file = nullptr;
        }
        
        locker.unlock();
        emit failed(m_taskId, m_errorString);
        return;
    }
    
    // 写入剩余数据
    if (m_file && m_reply) {
        m_file->write(m_reply->readAll());
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    
    m_reply->deleteLater();
    m_reply = nullptr;
    
    locker.unlock();
    emit finished(m_taskId);
}

void DownloadTask::onDownloadError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error);
    
    if (!m_reply) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    m_errorString = m_reply->errorString();
    
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    
    locker.unlock();
    emit failed(m_taskId, m_errorString);
}

void DownloadTask::onSegmentFinished(int segmentIndex)
{
    QMutexLocker locker(&m_mutex);
    
    m_completedSegments++;
    
    if (m_completedSegments == m_segmentCount) {
        // 所有分段下载完成，合并文件
        locker.unlock();
        mergeSegments();
    }
}

void DownloadTask::onSegmentError(int segmentIndex, const QString &error)
{
    QMutexLocker locker(&m_mutex);
    
    m_errorString = QString("Segment %1 download failed: %2").arg(segmentIndex).arg(error);
    
    // 取消所有其他分段
    for (auto segment : m_segments) {
        segment->cancel();
    }
    
    locker.unlock();
    emit failed(m_taskId, m_errorString);
}

void DownloadTask::onSegmentProgress(int segmentIndex, qint64 bytesReceived, qint64 bytesTotal)
{
    QMutexLocker locker(&m_mutex);
    
    if (segmentIndex >= 0 && segmentIndex < m_segmentProgress.size()) {
        m_segmentProgress[segmentIndex] = bytesReceived;
    }
    
    // 计算总进度
    qint64 totalReceived = 0;
    for (qint64 progress : m_segmentProgress) {
        totalReceived += progress;
    }
    
    m_downloadedSize = totalReceived;
    
    // 更新停滞检测的进度信息
    m_lastProgressBytes = totalReceived;
    m_lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    
    locker.unlock();
    emit progress(m_taskId, totalReceived, m_fileSize);
}

void DownloadTask::onStallCheck()
{
    QMutexLocker locker(&m_mutex);
    
    // 检查是否已取消或暂停
    if (m_isCanceled || m_isPaused) {
        return;
    }
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeSinceLastProgress = currentTime - m_lastProgressTime;
    
    // 如果超过停滞超时时间仍无进度，则视为停滞
    if (timeSinceLastProgress > m_stallTimeout) {
        qint64 currentBytes = m_downloadedSize;
        
        // 确认确实没有新数据
        if (currentBytes == m_lastProgressBytes) {
            m_errorString = QString("Download stalled: no progress for %1 seconds").arg(m_stallTimeout / 1000);
            
            // 停止定时器
            if (m_stallTimer) {
                m_stallTimer->stop();
            }
            
            // 取消当前连接
            if (m_reply) {
                m_reply->abort();
            }
            
            // 取消所有分段
            for (auto segment : m_segments) {
                segment->cancel();
            }
            
            locker.unlock();
            emit failed(m_taskId, m_errorString);
        }
    }
}

void DownloadTask::mergeSegments()
{
    QMutexLocker locker(&m_mutex);
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    // 打开目标文件
    QFile outFile(m_savePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        m_errorString = QString("Cannot create target file: %1").arg(m_savePath);
        locker.unlock();
        emit failed(m_taskId, m_errorString);
        return;
    }
    
    // 依次读取并写入每个分段文件
    for (int i = 0; i < m_segmentCount; ++i) {
        QString segmentPath = m_savePath + QString(".part%1").arg(i);
        QFile segmentFile(segmentPath);
        
        if (!segmentFile.open(QIODevice::ReadOnly)) {
            m_errorString = QString("Cannot open segment file: %1").arg(segmentPath);
            outFile.close();
            locker.unlock();
            emit failed(m_taskId, m_errorString);
            return;
        }
        
        // 读取并写入数据
        while (!segmentFile.atEnd()) {
            QByteArray data = segmentFile.read(8192);
            outFile.write(data);
        }
        
        segmentFile.close();
        segmentFile.remove();  // 删除分段文件
    }
    
    outFile.close();
    
    locker.unlock();
    emit finished(m_taskId);
}

// ==================== SegmentDownloader 实现 ====================

SegmentDownloader::SegmentDownloader(int index, const QUrl &url, const QString &filePath,
                                   qint64 start, qint64 end, int timeout, int stallTimeout, QObject *parent)
    : QObject(parent)
    , m_index(index)
    , m_url(url)
    , m_filePath(filePath)
    , m_start(start)
    , m_end(end)
    , m_bytesReceived(0)
    , m_lastProgressBytes(0)
    , m_lastProgressTime(0)
    , m_timeout(timeout)
    , m_stallTimeout(stallTimeout)
    , m_networkManager(nullptr)
    , m_reply(nullptr)
    , m_file(nullptr)
    , m_ownsNetworkManager(false)
    , m_stallTimer(nullptr)
    , m_isCanceled(false)
{
    // 尝试从DownloadTask的父对象（AsulMultiDownloader）获取共享的网络管理器
    DownloadTask *task = qobject_cast<DownloadTask*>(parent);
    if (task) {
        AsulMultiDownloader *downloader = qobject_cast<AsulMultiDownloader*>(task->parent());
        if (downloader) {
            m_networkManager = downloader->getNetworkManager();
            m_ownsNetworkManager = false;
        }
    }
    
    // 如果无法获取，创建自己的（兜底方案）
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
        m_ownsNetworkManager = true;
    }
    
    // 创建停滞检查定时器
    m_stallTimer = new QTimer(this);
    m_stallTimer->setSingleShot(false);
    connect(m_stallTimer, &QTimer::timeout, this, &SegmentDownloader::onStallCheck);
}

SegmentDownloader::~SegmentDownloader()
{
    cancel();
    
    // 注意：不需要显式释放网络管理器
    // - 如果是从池中借用的（m_ownsNetworkManager == false），池会一直持有这些管理器
    // - 如果是自己创建的（m_ownsNetworkManager == true），Qt的父子关系会自动删除
}

void SegmentDownloader::start()
{
    if (m_isCanceled) {
        return;
    }
    
    // 初始化停滞检测
    m_lastProgressBytes = 0;
    m_lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    
    // 打开文件
    m_file = new QFile(m_filePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        QString error = QString("Cannot open file: %1").arg(m_filePath);
        delete m_file;
        m_file = nullptr;
        emit this->error(m_index, error);
        return;
    }
    
    // 创建请求，设置Range头
    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeout);
    request.setRawHeader("Range", QString("bytes=%1-%2").arg(m_start).arg(m_end).toUtf8());
    
    m_reply = m_networkManager->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &SegmentDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &SegmentDownloader::onFinished);
    connect(m_reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &SegmentDownloader::onError);
    
    // 启动停滞检查定时器（每5秒检查一次）
    if (m_stallTimer) {
        m_stallTimer->start(5000);
    }
}

void SegmentDownloader::cancel()
{
    m_isCanceled = true;
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void SegmentDownloader::onReadyRead()
{
    if (!m_file || !m_reply) {
        return;
    }
    
    QByteArray data = m_reply->readAll();
    m_file->write(data);
    m_bytesReceived += data.size();
    
    // 更新停滞检测的进度信息
    m_lastProgressBytes = m_bytesReceived;
    m_lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    
    qint64 total = m_end - m_start + 1;
    emit progress(m_index, m_bytesReceived, total);
}

void SegmentDownloader::onFinished()
{
    if (!m_reply) {
        return;
    }
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    if (m_reply->error() != QNetworkReply::NoError) {
        QString errorString = m_reply->errorString();
        
        if (m_file) {
            m_file->close();
            delete m_file;
            m_file = nullptr;
            QFile::remove(m_filePath);
        }
        
        m_reply->deleteLater();
        m_reply = nullptr;
        
        emit error(m_index, errorString);
        return;
    }
    
    // 写入剩余数据
    if (m_file && m_reply) {
        QByteArray data = m_reply->readAll();
        m_file->write(data);
        m_bytesReceived += data.size();
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    
    m_reply->deleteLater();
    m_reply = nullptr;
    
    emit finished(m_index);
}

void SegmentDownloader::onError(QNetworkReply::NetworkError errorCode)
{
    Q_UNUSED(errorCode);
    
    if (!m_reply) {
        return;
    }
    
    // 停止停滞检查定时器
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    
    QString errorString = m_reply->errorString();
    
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
        QFile::remove(m_filePath);
    }
    
    emit error(m_index, errorString);
}

void SegmentDownloader::onStallCheck()
{
    // 检查是否已取消
    if (m_isCanceled) {
        return;
    }
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeSinceLastProgress = currentTime - m_lastProgressTime;
    
    // 如果超过停滞超时时间仍无进度，则视为停滞
    if (timeSinceLastProgress > m_stallTimeout) {
        qint64 currentBytes = m_bytesReceived;
        
        // 确认确实没有新数据
        if (currentBytes == m_lastProgressBytes) {
            QString errorString = QString("Segment %1 stalled: no progress for %2 seconds")
                                    .arg(m_index)
                                    .arg(m_stallTimeout / 1000);
            
            // 停止定时器
            if (m_stallTimer) {
                m_stallTimer->stop();
            }
            
            // 取消当前连接
            if (m_reply) {
                m_reply->abort();
            }
            
            emit error(m_index, errorString);
        }
    }
}

// ==================== PCL优化：监控和动态调度实现 ====================

void AsulMultiDownloader::onMonitorDownloads()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_speedMonitoringEnabled) {
        return;
    }
    
    // 计算当前速度
    qint64 currentSpeed = calculateCurrentSpeed();
    
    // 如果速度低于阈值，尝试为现有下载任务增加线程
    if (currentSpeed > 0 && currentSpeed < m_speedThreshold) {
        // 遍历正在下载的任务，尝试增加分段
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            if (m_taskStatus[it.key()] == DownloadStatus::Downloading) {
                auto task = it.value();
                
                // 检查是否可以增加更多线程
                if (m_activeDownloads < m_maxConcurrentDownloads) {
                    // 这里可以添加更复杂的逻辑来动态增加分段
                    // 当前实现中主要通过processQueue来处理
                }
            }
        }
    }
    
    // 尝试处理队列中的任务
    if (m_activeDownloads < m_maxConcurrentDownloads && !m_taskQueue.isEmpty()) {
        processQueue();
    }
}

qint64 AsulMultiDownloader::calculateCurrentSpeed()
{
    // 计算从上次检查到现在的平均速度
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 currentBytes = m_statistics.totalDownloaded;
    
    if (m_monitorLastTime == 0) {
        m_monitorLastTime = currentTime;
        m_monitorLastBytes = currentBytes;
        return 0;
    }
    
    qint64 timeDiff = currentTime - m_monitorLastTime;
    if (timeDiff < 100) {  // 至少间隔100ms
        return 0;
    }
    
    qint64 bytesDiff = currentBytes - m_monitorLastBytes;
    if (bytesDiff < 0) bytesDiff = 0; // 防止回绕
    
    qint64 speed = (bytesDiff * 1000) / timeDiff;  // 字节/秒
    
    // 更新上次检查的值（每秒更新一次）
    if (timeDiff >= 1000) {
        m_monitorLastTime = currentTime;
        m_monitorLastBytes = currentBytes;
    }
    
    return speed;
}

bool AsulMultiDownloader::shouldDisableMultiThread(const QUrl &url) const
{
    QString host = url.host().toLower();
    
    // 检查是否在禁用多线程的域名列表中
    for (const QString &pattern : m_noMultiThreadHosts) {
        if (host.contains(pattern.toLower())) {
            return true;
        }
    }
    
    return false;
}

QNetworkAccessManager* AsulMultiDownloader::getNetworkManager()
{
    // Fix: Remove mutex lock to avoid recursive locking deadlock
    // m_networkManagers is read-only after constructor, so this is safe
    
    if (m_networkManagers.isEmpty()) {
        return nullptr;
    }
    
    // 使用thread-safe的轮询分配
    // 原子计数器确保线程安全，modulo防止溢出
    static std::atomic<int> index{0};
    int currentIndex = index.fetch_add(1) % m_networkManagers.size();
    
    return m_networkManagers[currentIndex];
}

void AsulMultiDownloader::checkAndEmitAllFinished()
{
    // 注意：调用此方法时应已持有锁
    
    // 检查是否所有任务都完成了（包括成功和失败的任务）
    bool allFinished = true;
    for (auto status : m_taskStatus) {
        if (status == DownloadStatus::Queued || status == DownloadStatus::Downloading) {
            allFinished = false;
            break;
        }
    }
    
    if (allFinished && m_taskQueue.isEmpty() && !m_allFinishedEmitted) {
        m_allFinishedEmitted = true;
        
        // Stop timers to allow event loop to exit
        if (m_statisticsTimer) {
            m_statisticsTimer->stop();
        }
        if (m_monitorTimer) {
            m_monitorTimer->stop();
        }
        
        emit allDownloadsFinished();
    }
}
