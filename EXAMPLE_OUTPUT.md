# Example Output - AsulDownloader with Stall Detection

This document shows example output from AsulDownloader with the new stall detection and automatic retry features enabled.

## Startup Configuration

```
AsulDownloader - Minecraft Asset and Version Downloader
========================================================
Concurrency Config:
  Max Concurrent: 512
  Max Conn/Host: 512
Stall Detection:
  Stall Timeout: 15000 ms
  Max Retries: 3
  Auto Retry: Enabled

Parsing assets.json...
Found 4585 asset objects
Added 4585 new asset download tasks

Parsing version.json...
Version: 1.21.11
Added client.jar download: 1.21.11
Found 107 libraries
Added 95 new library download tasks

Total tasks added: 4680
Total size: 245.67 MB
Starting downloads...
```

## Normal Download Progress

```
[PROGRESS] 0 | 512 | 4680 | 0.00 MB | 245.67 MB | Speed: 0.00 MB/s
[PROGRESS] 512 | 512 | 4680 | 15.23 MB | 245.67 MB | Speed: 30.46 MB/s
[PROGRESS] 1024 | 512 | 4680 | 32.45 MB | 245.67 MB | Speed: 34.44 MB/s
[100/4680] Completed: a1b2c3d4e5f6...
[PROGRESS] 1536 | 512 | 4680 | 50.67 MB | 245.67 MB | Speed: 36.44 MB/s
[200/4680] Completed: 1234567890ab...
[PROGRESS] 2048 | 512 | 4680 | 68.89 MB | 245.67 MB | Speed: 36.44 MB/s
```

## When Stall Detection Triggers

```
[PROGRESS] 2560 | 512 | 4680 | 87.12 MB | 245.67 MB | Speed: 36.44 MB/s
[300/4680] Completed: fedcba098765...

# Connection stalls here - no progress for 15 seconds
# Stall timer detects it and triggers retry

[RETRY] Task task_0000234, Attempt 1 (Total retries: 1)
[RETRY] Task task_0000567, Attempt 1 (Total retries: 2)
[RETRY] Task task_0000891, Attempt 1 (Total retries: 3)

# Retried connections succeed and download continues

[PROGRESS] 2612 | 512 | 4680 | 89.34 MB | 245.67 MB | Speed: 35.78 MB/s
[PROGRESS] 3072 | 512 | 4680 | 105.56 MB | 245.67 MB | Speed: 36.12 MB/s
[400/4680] Completed: 9876543210fe...
```

## Multiple Retry Attempts

```
[PROGRESS] 3584 | 512 | 4680 | 123.78 MB | 245.67 MB | Speed: 36.44 MB/s

# Some connections have persistent issues requiring multiple retries

[RETRY] Task task_0001234, Attempt 1 (Total retries: 15)
[PROGRESS] 3596 | 512 | 4680 | 125.01 MB | 245.67 MB | Speed: 35.89 MB/s

# First retry fails, second retry triggered
[RETRY] Task task_0001234, Attempt 2 (Total retries: 16)
[PROGRESS] 3608 | 512 | 4680 | 126.23 MB | 245.67 MB | Speed: 35.56 MB/s

# Second retry succeeds!
[500/4680] Completed: c0d1e2f3a4b5...
[PROGRESS] 3620 | 512 | 4680 | 127.45 MB | 245.67 MB | Speed: 36.01 MB/s
```

## When Retries Are Exhausted

```
[PROGRESS] 4096 | 512 | 4680 | 145.67 MB | 245.67 MB | Speed: 36.44 MB/s

# Some connections fail even after all retries

[RETRY] Task task_0002345, Attempt 1 (Total retries: 30)
[PROGRESS] 4108 | 512 | 4680 | 146.89 MB | 245.67 MB | Speed: 35.78 MB/s

[RETRY] Task task_0002345, Attempt 2 (Total retries: 31)
[PROGRESS] 4120 | 512 | 4680 | 148.12 MB | 245.67 MB | Speed: 35.45 MB/s

[RETRY] Task task_0002345, Attempt 3 (Total retries: 32)
[PROGRESS] 4132 | 512 | 4680 | 149.34 MB | 245.67 MB | Speed: 35.12 MB/s

# All retries exhausted - marked as failed
[FAILED] Task task_0002345: Download stalled: no progress for 15 seconds

[PROGRESS] 4144 | 512 | 4680 | 150.56 MB | 245.67 MB | Speed: 35.89 MB/s
[600/4680] Completed: 5a6b7c8d9e0f...
```

## Download Completion

```
[PROGRESS] 4608 | 72 | 4680 | 240.12 MB | 245.67 MB | Speed: 35.23 MB/s
[PROGRESS] 4650 | 30 | 4680 | 243.45 MB | 245.67 MB | Speed: 33.45 MB/s
[PROGRESS] 4670 | 10 | 4680 | 245.23 MB | 245.67 MB | Speed: 31.23 MB/s
[4680/4680] Completed: minecraft_1.21.11.jar

========================================================
All downloads finished!
Completed: 4675, Failed: 5, Total: 4680
Retries triggered: 47
========================================================
```

## Interpretation of Output

### Progress Line Format
```
[PROGRESS] Downloaded | Active | Total | Downloaded MB | Total MB | Speed MB/s
```

- **Downloaded**: Completed + Failed files
- **Active**: Currently downloading files
- **Total**: Total files to download
- **Downloaded MB**: Total bytes downloaded
- **Total MB**: Total bytes to download
- **Speed MB/s**: Current download speed

### Retry Line Format
```
[RETRY] Task task_id, Attempt N (Total retries: X)
```

- **task_id**: Unique identifier for the stalled task
- **Attempt N**: Which retry attempt (1, 2, or 3)
- **Total retries**: Cumulative retry count across all tasks

### Completion Line Format
```
[N/Total] Completed: filename
```

- **N**: Number of completed files
- **Total**: Total files to download
- **filename**: Name of completed file (or hash)

### Failed Line Format
```
[FAILED] Task task_id: error_message
```

- **task_id**: Unique identifier for failed task
- **error_message**: Reason for failure

## Network Health Analysis

Based on the example output above:

### Metrics
- **Total Tasks**: 4680
- **Completed**: 4675
- **Failed**: 5
- **Success Rate**: 99.89%
- **Total Retries**: 47
- **Retry Rate**: 1.00%

### Interpretation

**Good Network Health:**
- 99.89% success rate is excellent
- Only 47 retries out of 4680 tasks (1%)
- Most retries succeeded on first or second attempt
- Only 5 tasks failed after exhausting all retries

**What This Means:**
- Network had occasional hiccups (47 stalls detected)
- Automatic retry successfully recovered 42 stalled downloads
- Only 5 downloads were permanently problematic
- Stall detection is working correctly

### Poor Network Health Example

If output showed:
```
Completed: 4500, Failed: 180, Total: 4680
Retries triggered: 850
```

This would indicate:
- 18% retry rate (850/4680) - very high
- 3.8% failure rate (180/4680) - problematic
- Network is very unstable
- Consider increasing stall timeout or retry count

## Configuration Tuning Based on Output

### If Many Retries (>5% of tasks)
**Problem**: Network is unstable or timeout too aggressive

**Solution**:
```cpp
downloader.setStallTimeout(30000);   // Increase to 30 seconds
downloader.setMaxRetryCount(5);      // More retry attempts
```

### If Many Permanent Failures (>1% of tasks)
**Problem**: Server issues or URLs invalid

**Solution**:
- Check server availability
- Verify JSON files are correct
- Increase retry count temporarily

### If No Retries But Slow
**Problem**: Network is slow but stable

**Solution**:
- Current configuration is optimal
- No changes needed
- Slow speed is network limitation, not stall issue

### If Frequent Retry Spam
**Problem**: Stall timeout too aggressive

**Solution**:
```cpp
downloader.setStallTimeout(20000);   // Increase to 20 seconds
```

## Real-World Scenarios

### Scenario 1: Home WiFi (Good)
```
Completed: 4678, Failed: 2, Total: 4680
Retries triggered: 12
Average Speed: 35 MB/s
```
**Analysis**: Excellent performance, minimal retries, very stable

### Scenario 2: Mobile Hotspot (Moderate)
```
Completed: 4640, Failed: 40, Total: 4680
Retries triggered: 320
Average Speed: 12 MB/s
```
**Analysis**: Unstable connection, many retries, but mostly successful

### Scenario 3: Public WiFi (Poor)
```
Completed: 4450, Failed: 230, Total: 4680
Retries triggered: 980
Average Speed: 5 MB/s
```
**Analysis**: Very unstable, high failure rate, needs configuration tuning

### Scenario 4: Office Network (Excellent)
```
Completed: 4680, Failed: 0, Total: 4680
Retries triggered: 2
Average Speed: 50 MB/s
```
**Analysis**: Perfect conditions, virtually no issues

## Debugging Tips

### To See More Detail
Add this after creating downloader:
```cpp
QObject::connect(&downloader, &AsulMultiDownloader::downloadStarted,
                 [](const QString &taskId) {
    qDebug() << "[START]" << taskId;
});
```

### To Track Individual Files
```cpp
QObject::connect(&downloader, &AsulMultiDownloader::downloadProgress,
                 [](const QString &taskId, qint64 received, qint64 total) {
    if (received > 0 && total > 0) {
        double percent = (received * 100.0) / total;
        qDebug() << "[" << taskId << "]" << percent << "%";
    }
});
```

### To Log Retry Reasons
```cpp
QObject::connect(&downloader, &AsulMultiDownloader::downloadFailed,
                 [](const QString &taskId, const QString &errorString) {
    if (errorString.contains("stalled")) {
        qDebug() << "[STALL]" << taskId << errorString;
    }
});
```

## Summary

The new stall detection output provides:
- ✅ Clear visibility into retry events
- ✅ Total retry count for network health assessment
- ✅ Individual retry attempts per task
- ✅ Success/failure statistics at completion

Users can now:
- Monitor download health in real-time
- Identify network issues quickly
- Tune configuration based on observed behavior
- Trust that stalled downloads will be handled automatically
