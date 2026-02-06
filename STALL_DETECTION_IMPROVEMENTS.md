# Stall Detection and Dirty Connection Fixes

## Problem Statement (Chinese)
虽然这个版本的下载库能达到我的一半要求，但是他还是出现了连接数不稳以及很多未成功建立TCP连接的脏连接（始终速度为0.00MB/s）占据了,导致下载速度还是不够快并且在下载的末期出现资源下载始终为 0.00MB/s 既不报错也不重试必须重启程序才可以下载的窘境，请针对以上问题做出修改

**Translation:**
Although this version of the download library can meet half of my requirements, it still has issues with unstable connection counts and many unsuccessful TCP connections (dirty connections that always show 0.00MB/s speed) taking up resources, causing download speed to still not be fast enough. In the late stages of downloading, resources download at a constant 0.00MB/s without errors or retries, and the program must be restarted to continue downloading. Please make modifications to address these issues.

## Problems Identified

### 1. Dirty Connections (0.00MB/s)
**Symptom:** Connections that fail to establish TCP properly or get stuck continue to occupy connection slots without transferring data.

**Root Cause:** No mechanism to detect and cleanup connections that have stopped making progress.

### 2. Downloads Stuck in Late Stages
**Symptom:** Downloads hang at 0.00MB/s without error or retry in the final stages.

**Root Cause:** 
- No timeout for connections that have stopped transferring data
- Existing `setTransferTimeout()` only handles complete connection timeout, not stalls
- No automatic detection of progress stagnation

### 3. No Automatic Recovery
**Symptom:** Must restart the program to continue downloading.

**Root Cause:** Stalled connections don't trigger the existing retry mechanism.

## Solutions Implemented

### 1. Stall Detection Mechanism

Added comprehensive stall detection that monitors actual data transfer progress:

**New Configuration Parameter:**
```cpp
void setStallTimeout(int msecs);  // Default: 15000ms (15 seconds)
int stallTimeout() const;
```

**How It Works:**
- Each `DownloadTask` and `SegmentDownloader` now tracks:
  - `m_lastProgressBytes`: Bytes received at last progress update
  - `m_lastProgressTime`: Timestamp of last progress update
  - `m_stallTimer`: QTimer that checks progress every 5 seconds

- Every 5 seconds, the stall checker compares:
  - Current bytes vs. last recorded bytes
  - Current time vs. last progress time
  - If no progress for > `stallTimeout` (default 15s), connection is aborted

### 2. Progress Tracking in DownloadTask

**Added Members:**
```cpp
qint64 m_lastProgressBytes;  // Track progress bytes
qint64 m_lastProgressTime;   // Track progress timestamp
QTimer *m_stallTimer;        // 5-second check timer
```

**New Method:**
```cpp
void DownloadTask::onStallCheck()
{
    // Check if time since last progress > stallTimeout
    if (timeSinceLastProgress > m_stallTimeout) {
        // Verify no new data
        if (currentBytes == m_lastProgressBytes) {
            // Abort connection and emit failed signal
            // This triggers the existing retry mechanism
        }
    }
}
```

**Progress Updates:**
- `onDownloadProgress()`: Updates `m_lastProgressBytes` and `m_lastProgressTime`
- `onSegmentProgress()`: Same for segmented downloads
- Stall timer starts when download begins
- Stall timer stops when download completes/fails/pauses

### 3. Progress Tracking in SegmentDownloader

Same mechanism applied to each segment:

**Added Members:**
```cpp
qint64 m_lastProgressBytes;
qint64 m_lastProgressTime;
int m_stallTimeout;          // Passed from parent
QTimer *m_stallTimer;
```

**New Method:**
```cpp
void SegmentDownloader::onStallCheck()
{
    // Check segment-specific progress
    // Emit error if stalled, which propagates to DownloadTask
}
```

### 4. Integration with Existing Retry Mechanism

**Key Integration Points:**

1. **Stall detection triggers failure:**
   ```cpp
   emit failed(m_taskId, "Download stalled: no progress for X seconds");
   ```

2. **Existing retry logic handles it:**
   ```cpp
   void AsulMultiDownloader::onTaskFailed(const QString &taskId, const QString &error)
   {
       if (m_autoRetry && m_taskRetryCount[taskId] < m_maxRetryCount) {
           m_taskRetryCount[taskId]++;
           m_taskStatus[taskId] = DownloadStatus::Queued;
           m_taskQueue.enqueue(taskId);
           emit downloadRetrying(taskId, m_taskRetryCount[taskId]);
           processQueue();
       } else {
           // Mark as failed
       }
   }
   ```

3. **Dirty connections are automatically cleaned up and retried**

## Configuration

### Default Values
```cpp
m_stallTimeout = 15000;  // 15 seconds
m_maxRetryCount = 3;     // 3 retries
m_autoRetry = true;      // Enabled by default
```

### Customization
```cpp
AsulMultiDownloader downloader;

// Adjust stall timeout (more aggressive)
downloader.setStallTimeout(10000);  // 10 seconds

// Increase retries for flaky networks
downloader.setMaxRetryCount(5);

// Adjust transfer timeout (different from stall timeout)
downloader.setDownloadTimeout(60000);  // 60 seconds
```

### Differences: Transfer Timeout vs Stall Timeout

**Transfer Timeout (`setDownloadTimeout`):**
- Applies to the entire request
- Triggers if NO data received within timeout period
- Handled by Qt's `QNetworkRequest::setTransferTimeout()`
- Good for detecting complete connection failures

**Stall Timeout (`setStallTimeout`):**
- Monitors ongoing data transfer
- Triggers if data transfer STOPS for timeout period
- Handled by our custom stall detection
- Good for detecting stuck/dirty connections that initially worked

**Why Both Are Needed:**
- Transfer timeout catches failed connections early
- Stall timeout catches connections that start but then hang
- Together they provide comprehensive coverage

## Behavioral Changes

### Before Fix
1. Connection starts downloading
2. Connection stalls (0.00MB/s)
3. Connection occupies slot indefinitely
4. No error, no retry
5. Download appears stuck
6. User must restart program

### After Fix
1. Connection starts downloading
2. Connection stalls (0.00MB/s)
3. After 5 seconds: Stall checker notices no progress
4. After 15 seconds: Stall timeout exceeded
5. Connection aborted, marked as failed
6. Automatic retry triggered (up to 3 times)
7. New connection established
8. Download continues

### Edge Cases Handled

**Case 1: Slow but active connection**
- Progress updates reset `m_lastProgressTime`
- Even 1 byte of progress prevents stall detection
- Only truly stuck connections are caught

**Case 2: Large file with intermittent data**
- Each data chunk updates progress
- Stall timeout is per-chunk, not total download time
- Handles networks with variable throughput

**Case 3: Segmented download with one stuck segment**
- Each segment monitored independently
- One stuck segment triggers error for entire task
- Entire task retried (all segments restart)
- Prevents partial downloads

**Case 4: Multiple retries exhausted**
- After 3 retries, task marked as failed
- Doesn't infinitely retry
- Prevents infinite loops on permanently broken URLs

## Performance Impact

### Timer Overhead
- **Per Task:** 1 timer checking every 5 seconds
- **Total Timers:** ~512 max (one per concurrent download)
- **CPU Impact:** Negligible (simple comparison operation)
- **Memory Impact:** ~50 bytes per task (2 qint64 + 1 QTimer pointer)

### Network Efficiency
- **Connection Slots:** Freed faster when stuck
- **Bandwidth Utilization:** Better (fewer idle connections)
- **Retry Efficiency:** Failed connections retry automatically
- **Overall Speed:** Improved due to better connection management

## Testing Recommendations

### Test Case 1: Simulated Stall
```cpp
// Use a test server that sends data then stops
// Verify stall detection after 15 seconds
// Verify automatic retry
```

### Test Case 2: Flaky Network
```cpp
// Use a network simulator with intermittent connectivity
// Verify downloads complete despite interruptions
// Check retry count stays within limits
```

### Test Case 3: High Concurrency
```cpp
// Download 1000+ small files
// Verify no deadlocks from stall timers
// Check connection pool efficiency
```

### Test Case 4: Late-Stage Stalls
```cpp
// Download large file (>100MB)
// Interrupt connection at 95% complete
// Verify stall detection and retry
// Verify download completes
```

## Compatibility

### Backward Compatibility
- **API:** Fully backward compatible
- **Existing Code:** No changes required
- **Default Behavior:** More robust with no breaking changes

### Qt Version Compatibility
- **Minimum:** Qt 5.15 (QTimer, QDateTime)
- **Recommended:** Qt 6.7.3
- **Features Used:**
  - QTimer with timeout signals
  - QDateTime::currentMSecsSinceEpoch()
  - QMutexLocker for thread safety

## Known Limitations

### 1. Granularity
- Stall check runs every 5 seconds
- Minimum detection time: 15 seconds
- Cannot detect sub-second stalls

### 2. Retry Strategy
- Simple retry (restart from beginning)
- No incremental backoff
- All retries use same parameters

### 3. Partial Progress
- Segmented downloads restart all segments
- No per-segment retry
- Downloaded data discarded on retry

## Future Enhancements

### 1. Adaptive Stall Timeout
```cpp
// Adjust based on file size and network conditions
if (fileSize > 100 * 1024 * 1024) {  // 100MB
    stallTimeout = 30000;  // More lenient for large files
} else {
    stallTimeout = 10000;  // Aggressive for small files
}
```

### 2. Exponential Backoff
```cpp
int retryDelay = 1000 * (1 << retryCount);  // 1s, 2s, 4s, 8s...
QTimer::singleShot(retryDelay, [this]() { 
    retry(); 
});
```

### 3. Per-Segment Retry
```cpp
// Only retry failed segments
// Keep successfully downloaded segments
// Requires persistent segment storage
```

### 4. Connection Health Metrics
```cpp
struct ConnectionHealth {
    qint64 avgSpeed;
    int stallCount;
    double reliability;
};
// Use to prioritize good connections
```

## Summary

This update addresses the core issues:

✅ **Dirty connections detected and cleaned up**
- 15-second stall timeout catches stuck connections
- Progress monitoring at 5-second intervals

✅ **Automatic retry prevents manual restarts**
- Stalled connections trigger retry mechanism
- Up to 3 retries per task

✅ **Late-stage stalls handled**
- Progress monitoring continues until completion
- No special cases for download progress percentage

✅ **Minimal performance impact**
- Lightweight timer-based checks
- No blocking operations
- Thread-safe implementation

The download library is now significantly more robust and can handle unstable network conditions, dirty connections, and late-stage stalls without manual intervention.
