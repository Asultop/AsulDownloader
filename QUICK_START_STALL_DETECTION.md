# Quick Start Guide - Stall Detection and Dirty Connection Fixes

## What Was Fixed

This update solves three critical problems:

1. **Dirty connections (0.00MB/s)** - Connections that failed to establish or got stuck no longer occupy connection slots forever
2. **Downloads stuck in late stages** - Downloads that hang at 0.00MB/s now automatically retry instead of requiring program restart
3. **No automatic recovery** - The system now automatically detects and retries stalled connections

## How It Works

### Automatic Stall Detection

Every active download is now monitored:
- **Check Interval:** Every 5 seconds
- **Stall Timeout:** 15 seconds of no progress (configurable)
- **Action:** Automatic abort and retry (up to 3 times)

### What Gets Monitored

Both single-file downloads and segmented downloads are monitored:
- Individual file downloads
- Each segment of large file downloads
- Progress is tracked in real-time

## Using the New Features

### Default Configuration (Recommended)

The defaults work well for most scenarios:

```cpp
AsulMultiDownloader downloader;
// Uses these defaults:
// - Stall timeout: 15 seconds
// - Max retries: 3
// - Auto retry: enabled
```

No code changes needed - the new features work automatically!

### Custom Configuration

#### Adjust Stall Timeout

Make it more aggressive (detect stalls faster):
```cpp
downloader.setStallTimeout(10000);  // 10 seconds
```

Make it more lenient (for slow/flaky networks):
```cpp
downloader.setStallTimeout(30000);  // 30 seconds
```

#### Adjust Retry Count

For unreliable networks:
```cpp
downloader.setMaxRetryCount(5);  // Try up to 5 times
```

For fast-fail behavior:
```cpp
downloader.setMaxRetryCount(1);  // Only try once
```

#### Disable Auto-Retry (Not Recommended)

```cpp
downloader.setAutoRetry(false);  // Stalls will just fail
```

### Monitoring Stall Events

Listen for retry signals to track stalled connections:

```cpp
QObject::connect(&downloader, &AsulMultiDownloader::downloadRetrying,
                 [](const QString &taskId, int retryCount) {
    qDebug() << "Retrying task" << taskId 
             << "attempt" << retryCount;
});
```

## Configuration Examples

### Example 1: Home Network (Stable)
```cpp
AsulMultiDownloader downloader;
downloader.setStallTimeout(15000);    // 15 seconds
downloader.setMaxRetryCount(3);       // 3 retries
// Good balance for stable home internet
```

### Example 2: Mobile Network (Flaky)
```cpp
AsulMultiDownloader downloader;
downloader.setStallTimeout(30000);    // 30 seconds (more lenient)
downloader.setMaxRetryCount(5);       // 5 retries
// More patience for unstable mobile connections
```

### Example 3: Fast Datacenter Network
```cpp
AsulMultiDownloader downloader;
downloader.setStallTimeout(5000);     // 5 seconds (aggressive)
downloader.setMaxRetryCount(2);       // 2 retries
// Fast detection for high-speed connections
```

### Example 4: Large File Downloads
```cpp
AsulMultiDownloader downloader;
downloader.setStallTimeout(60000);    // 60 seconds
downloader.setMaxRetryCount(5);       // 5 retries
downloader.setDownloadTimeout(120000); // 2 minutes transfer timeout
// Very large files need more patience
```

## Understanding Timeouts

There are TWO different timeouts:

### Transfer Timeout (Already Existed)
```cpp
downloader.setDownloadTimeout(30000);  // Default: 30 seconds
```
- Triggers if NO data received within timeout
- Catches complete connection failures early
- Handled by Qt networking

### Stall Timeout (NEW)
```cpp
downloader.setStallTimeout(15000);  // Default: 15 seconds
```
- Triggers if data transfer STOPS for timeout
- Catches connections that start but then hang
- Handled by our custom monitoring

**Both are important!** Transfer timeout catches early failures, stall timeout catches mid-download issues.

## Expected Behavior Changes

### Before This Fix

```
Download starts → Connection stalls → Sits at 0.00MB/s forever
                                    ↓
                            User must restart program
```

### After This Fix

```
Download starts → Connection stalls → Detected after 15s → Automatically retried
                                                          ↓
                                                   Success after retry
```

## Performance Impact

### Minimal Overhead
- **CPU:** Negligible (one comparison every 5 seconds per download)
- **Memory:** ~50 bytes per download
- **Network:** Better utilization (fewer idle connections)

### Improved Efficiency
- ✅ Faster completion (stuck connections don't block slots)
- ✅ Better bandwidth usage (no idle connections)
- ✅ Higher success rate (automatic retries)

## Troubleshooting

### Too Many Retries

**Problem:** Downloads keep retrying and failing
**Solution:** 
```cpp
// Increase stall timeout for slow networks
downloader.setStallTimeout(30000);  // 30 seconds

// Or check your network connection
```

### Downloads Still Hanging

**Problem:** Some downloads still hang despite stall detection
**Possible Causes:**
1. Stall timeout too high
2. Network issues before data transfer starts
3. Server-side problems

**Solution:**
```cpp
// Lower stall timeout
downloader.setStallTimeout(10000);  // 10 seconds

// Also check transfer timeout
downloader.setDownloadTimeout(20000);  // 20 seconds
```

### False Positives

**Problem:** Downloads failing due to "stall" on slow connections
**Solution:**
```cpp
// Increase stall timeout
downloader.setStallTimeout(30000);  // 30 seconds

// Increase retry count for safety
downloader.setMaxRetryCount(5);
```

## Migration from Previous Version

### No Code Changes Required!

The new features are backward compatible:

```cpp
// Old code still works
AsulMultiDownloader downloader;
downloader.addDownload(url, savePath);
// Now has automatic stall detection!
```

### Optional: Add Retry Monitoring

```cpp
// Optional: Track retries for debugging
QObject::connect(&downloader, &AsulMultiDownloader::downloadRetrying,
                 [](const QString &taskId, int retryCount) {
    qDebug() << "Retry:" << taskId << "attempt" << retryCount;
});
```

## Testing Your Configuration

### Test Case 1: Basic Functionality
```cpp
// Download a small file
downloader.addDownload(QUrl("https://example.com/file.zip"), "test.zip");

// Should complete normally
// Check logs for any retries
```

### Test Case 2: Simulate Network Issue
```cpp
// Start download
// Disconnect network after 50% progress
// Reconnect network
// Should detect stall and retry
```

### Test Case 3: High Concurrency
```cpp
// Add 1000+ small files
QList<QUrl> urls = ...;  // Many URLs
QStringList paths = ...; // Many paths
downloader.addDownloads(urls, paths);

// Monitor for stalls and retries
// Verify all files download successfully
```

## Best Practices

### 1. Use Defaults for Most Cases
```cpp
AsulMultiDownloader downloader;
// Defaults are tuned for typical internet connections
```

### 2. Tune for Your Network
```cpp
// Measure your network's behavior first
// Adjust timeouts based on actual performance
```

### 3. Monitor Retry Counts
```cpp
// Log retries to understand network quality
QObject::connect(&downloader, &AsulMultiDownloader::downloadRetrying,
                 [](const QString &taskId, int retryCount) {
    qWarning() << "Retry detected - network may be unstable";
});
```

### 4. Set Appropriate Concurrency
```cpp
// Don't overwhelm network with too many connections
downloader.setMaxConcurrentDownloads(32);  // Reasonable for most networks
```

### 5. Test in Production-Like Environment
```cpp
// Test with:
// - Actual file sizes you'll download
// - Actual network conditions
// - Actual concurrent download count
```

## Summary

The stall detection feature provides:

✅ **Automatic cleanup** of dirty connections  
✅ **Automatic retry** for stalled downloads  
✅ **No manual intervention** required  
✅ **Configurable** timeouts and retry counts  
✅ **Backward compatible** with existing code  
✅ **Minimal overhead** on system resources  

Your downloads are now more robust and reliable!

## Support

For issues or questions:
1. Check STALL_DETECTION_IMPROVEMENTS.md for technical details
2. Review configuration examples above
3. Monitor retry logs for debugging
4. Adjust timeouts based on your network

## Version Information

- **Feature:** Stall Detection and Automatic Retry
- **Added:** 2024
- **Minimum Qt:** 5.15
- **Recommended Qt:** 6.7.3
- **C++ Standard:** C++17
