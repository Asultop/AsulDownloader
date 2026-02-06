# main.cpp Updates for Stall Detection Features

## Summary

Updated `main.cpp` to utilize the new stall detection and automatic retry features added to AsulMultiDownloader. These changes enable the application to automatically handle dirty connections and stalled downloads without manual intervention.

## Changes Made

### 1. Configuration Section (Lines 54-65)

Added configuration for the new stall detection features:

```cpp
// 新增：配置停滞检测和自动重试（解决脏连接和下载停滞问题）
downloader.setStallTimeout(15000);      // 15秒无数据传输则视为停滞
downloader.setMaxRetryCount(3);         // 最多重试3次
downloader.setAutoRetry(true);          // 启用自动重试
```

**What it does:**
- **setStallTimeout(15000)**: Sets the stall timeout to 15 seconds. If a connection has no data transfer for 15 seconds, it's considered stalled and will be aborted.
- **setMaxRetryCount(3)**: Sets the maximum number of retry attempts to 3. After 3 failed attempts, the download is marked as permanently failed.
- **setAutoRetry(true)**: Enables automatic retry (this is the default, but explicitly set for clarity).

**Why these values:**
- 15 seconds is a good balance - not too aggressive (won't falsely detect slow downloads as stalled) and not too lenient (won't wait too long for truly stuck connections)
- 3 retries gives enough chances for transient network issues while preventing infinite retry loops

### 2. Configuration Output (Lines 62-65)

Added display of stall detection settings at startup:

```cpp
qDebug() << "Stall Detection:";
qDebug() << "  Stall Timeout:" << downloader.stallTimeout() << "ms";
qDebug() << "  Max Retries:" << downloader.maxRetryCount();
qDebug() << "  Auto Retry:" << (downloader.autoRetry() ? "Enabled" : "Disabled");
```

**Output Example:**
```
Stall Detection:
  Stall Timeout: 15000 ms
  Max Retries: 3
  Auto Retry: Enabled
```

### 3. Retry Monitoring (Lines 91-98)

Added signal handler to track and log retry events:

```cpp
// 新增：监控重试事件（停滞检测触发的自动重试）
int retryCount = 0;
QObject::connect(&downloader, &AsulMultiDownloader::downloadRetrying,
                 [&](const QString &taskId, int retryAttempt) {
    retryCount++;
    qDebug() << QString("[RETRY] Task %1, Attempt %2 (Total retries: %3)")
                .arg(taskId).arg(retryAttempt).arg(retryCount);
});
```

**What it does:**
- Connects to the `downloadRetrying` signal emitted when a stalled connection is retried
- Tracks total number of retries across all downloads
- Logs each retry event with task ID and attempt number

**Example Output:**
```
[RETRY] Task task_12345, Attempt 1 (Total retries: 5)
[RETRY] Task task_67890, Attempt 2 (Total retries: 6)
```

### 4. Final Statistics (Line 133)

Added retry count to final summary:

```cpp
qDebug() << QString("Retries triggered: %1").arg(retryCount);
```

**Output Example:**
```
========================================================
All downloads finished!
Completed: 4680, Failed: 5, Total: 4685
Retries triggered: 23
========================================================
```

### 5. Header Comments (Lines 13-14)

Added documentation of new features:

```cpp
 * - Automatic stall detection and retry (NEW)
 * - Dirty connection cleanup (NEW)
```

## Benefits of These Changes

### 1. Automatic Recovery
- No manual intervention needed when downloads stall
- Program continues automatically instead of requiring restart

### 2. Better Visibility
- Users can see when retries occur
- Final statistics show how many connections had issues
- Configuration is clearly displayed at startup

### 3. Network Health Monitoring
- Retry count indicates network stability
- High retry count suggests network issues
- Can adjust configuration based on observed behavior

### 4. Improved Success Rate
- Transient network issues are handled automatically
- Downloads complete successfully despite temporary problems
- Better utilization of connection pool (stuck connections freed quickly)

## Configuration Tuning

Users can adjust the configuration in `main.cpp` based on their network conditions:

### For Stable Networks
```cpp
downloader.setStallTimeout(10000);   // 10 seconds (more aggressive)
downloader.setMaxRetryCount(2);      // 2 retries (faster failure)
```

### For Unstable/Slow Networks
```cpp
downloader.setStallTimeout(30000);   // 30 seconds (more lenient)
downloader.setMaxRetryCount(5);      // 5 retries (more persistent)
```

### For Testing/Debugging
```cpp
downloader.setStallTimeout(5000);    // 5 seconds (very aggressive)
downloader.setMaxRetryCount(1);      // 1 retry (fast fail)
```

## Expected Behavior

### Before These Changes
1. Connection stalls at 0.00MB/s
2. Occupies connection slot indefinitely
3. No error reported
4. User must restart program

### After These Changes
1. Connection stalls at 0.00MB/s
2. Detected after 15 seconds
3. Connection aborted and retried
4. `[RETRY]` message logged
5. Download completes automatically

## Monitoring During Execution

### Normal Operation
```
[PROGRESS] 1000 | 512 | 5000 | 150.23 MB | 500.00 MB | Speed: 5.23 MB/s
[PROGRESS] 1100 | 512 | 5000 | 165.45 MB | 500.00 MB | Speed: 5.18 MB/s
```

### When Stall Detected
```
[PROGRESS] 1200 | 512 | 5000 | 180.67 MB | 500.00 MB | Speed: 4.95 MB/s
[RETRY] Task task_abc123, Attempt 1 (Total retries: 1)
[PROGRESS] 1250 | 512 | 5000 | 190.12 MB | 500.00 MB | Speed: 5.01 MB/s
```

### If Retry Succeeds
```
[1250/5000] Completed: resource_file.png
[PROGRESS] 1300 | 512 | 5000 | 200.34 MB | 500.00 MB | Speed: 5.15 MB/s
```

### If All Retries Exhausted
```
[RETRY] Task task_def456, Attempt 1 (Total retries: 10)
[RETRY] Task task_def456, Attempt 2 (Total retries: 11)
[RETRY] Task task_def456, Attempt 3 (Total retries: 12)
[FAILED] Task task_def456: Download stalled: no progress for 15 seconds
```

## Code Quality

### Thread Safety
- All signal connections are thread-safe (Qt's signal/slot mechanism)
- `retryCount` is captured by reference and updated in main thread

### Memory Management
- No memory leaks (all objects managed by Qt parent-child relationships)
- Lambda captures are safe (capturing by reference for stack variables)

### Error Handling
- Retry count is tracked separately from failed task count
- Retries are logged but don't increment failed count until exhausted

## Testing Recommendations

### Test Case 1: Normal Downloads
- Run with stable network
- Verify retry count remains at 0
- All downloads should complete without retries

### Test Case 2: Network Interruption
- Start downloads
- Disconnect network briefly
- Reconnect network
- Verify retries are logged
- Downloads should complete after retry

### Test Case 3: Persistent Network Issues
- Run with very unreliable network
- Verify retries occur (max 3 per task)
- Some tasks may fail after exhausting retries
- Final statistics should show retry count

### Test Case 4: Configuration Changes
- Try different timeout values (5s, 15s, 30s)
- Observe behavior differences
- Verify configuration is displayed correctly

## Integration with Existing Code

### Backward Compatibility
- All changes are additions (no modifications to existing behavior)
- Default values work well for most cases
- Existing error handling preserved

### Signal Connections
- New signal handler added alongside existing ones
- Does not interfere with existing downloadFinished/downloadFailed handlers
- All statistics remain accurate

### Configuration Flow
1. Create downloader
2. Configure all settings (existing + new)
3. Display configuration
4. Connect signal handlers
5. Start downloads

## Troubleshooting

### High Retry Count
**Problem:** Many retries occurring  
**Possible Causes:**
- Network is unstable
- Stall timeout too aggressive
- Server is rate-limiting

**Solutions:**
- Increase stall timeout
- Reduce concurrent downloads
- Check network stability

### No Retries Despite Known Issues
**Problem:** Downloads failing but no retries logged  
**Possible Causes:**
- Auto-retry is disabled
- Failures are immediate (not stalls)
- Transfer timeout triggering before stall timeout

**Solutions:**
- Verify autoRetry is true
- Check error messages in [FAILED] logs
- Adjust transfer timeout

### Retries Exhausted for Many Tasks
**Problem:** Many tasks failing after 3 retries  
**Possible Causes:**
- Network is very unstable
- Server is down
- URLs are incorrect

**Solutions:**
- Increase max retry count
- Check server availability
- Verify URLs in JSON files

## Future Enhancements

Possible improvements to consider:

1. **Dynamic Timeout Adjustment**
   ```cpp
   // Adjust based on observed network speed
   if (averageSpeed < 100KB/s) {
       downloader.setStallTimeout(30000);
   }
   ```

2. **Retry Statistics Breakdown**
   ```cpp
   struct RetryStats {
       int totalRetries;
       int successfulRetries;
       int failedRetries;
       QMap<QString, int> retriesByTask;
   };
   ```

3. **Retry Logging to File**
   ```cpp
   QFile retryLog("retry_log.txt");
   // Log all retry events for later analysis
   ```

4. **Network Health Score**
   ```cpp
   double healthScore = (completedTasks - retryCount) / completedTasks;
   qDebug() << "Network Health:" << (healthScore * 100) << "%";
   ```

## Summary

The updates to `main.cpp` are minimal but effective:
- ✅ 3 lines of configuration
- ✅ 4 lines of configuration output  
- ✅ 7 lines for retry monitoring
- ✅ 1 line for retry statistics
- ✅ 2 lines for header comments

**Total: 17 lines of actual code changes**

These small changes enable powerful automatic recovery from connection issues, making the downloader much more robust and user-friendly.
