# Average Speed Display on Completion

## Feature Description

This update adds average download speed information when all downloads are finished.

## Changes Made

### main.cpp

**Added tracking variables (line 72):**
```cpp
QDateTime startTime;    // Track when downloads start
```

**Record start time (lines 188-189):**
```cpp
// Record start time for average speed calculation
startTime = QDateTime::currentDateTime();
```

**Calculate and display average speed (lines 131-148):**
```cpp
// Calculate average speed
QDateTime endTime = QDateTime::currentDateTime();
qint64 elapsedMs = startTime.msecsTo(endTime);
double elapsedSeconds = elapsedMs / 1000.0;

// Calculate average speed in MB/s
double avgSpeedMBps = 0.0;
if (elapsedSeconds > 0 && totalBytes > 0) {
    avgSpeedMBps = (totalBytes / (1024.0 * 1024.0)) / elapsedSeconds;
}

qDebug() << QString("Total time: %1 seconds").arg(elapsedSeconds, 0, 'f', 2);
qDebug() << QString("Average speed: %1 MB/s").arg(avgSpeedMBps, 0, 'f', 2);
```

## Example Output

### Before:
```
========================================================
All downloads finished!
Completed: 4675, Failed: 5, Total: 4680
Retries triggered: 47
========================================================
```

### After:
```
========================================================
All downloads finished!
Completed: 4675, Failed: 5, Total: 4680
Retries triggered: 47
Total time: 125.34 seconds
Average speed: 1.96 MB/s
========================================================
```

## Implementation Details

### Timing
- **Start Time**: Recorded when `statsTimer->start(500)` is called (right after "Starting downloads...")
- **End Time**: Captured when `allDownloadsFinished` signal is emitted
- **Elapsed Time**: Calculated using `QDateTime::msecsTo()` for millisecond precision

### Speed Calculation
```
Average Speed (MB/s) = Total Downloaded (MB) / Elapsed Time (seconds)
```

Where:
- `Total Downloaded (MB)` = `totalBytes / (1024.0 * 1024.0)`
- `Elapsed Time (seconds)` = `elapsedMs / 1000.0`

### Safety Checks
- Division by zero protection: checks `elapsedSeconds > 0`
- No data protection: checks `totalBytes > 0`
- Returns 0.0 MB/s if either condition is false

## Benefits

1. **Performance Insight**: Users can see overall download performance
2. **Network Health**: Average speed indicates overall network quality
3. **Comparison**: Users can compare performance across different runs
4. **Troubleshooting**: Low average speed can indicate network issues

## Use Cases

### Normal Download
```
Total time: 125.34 seconds
Average speed: 1.96 MB/s
```
Indicates good performance for a 245 MB download.

### Slow Network
```
Total time: 500.78 seconds
Average speed: 0.49 MB/s
```
Indicates network congestion or throttling.

### Fast Network
```
Total time: 30.12 seconds
Average speed: 8.15 MB/s
```
Indicates excellent network performance.

### Instant Completion (All Cached)
```
Total time: 0.50 seconds
Average speed: 0.00 MB/s
```
All files were already downloaded (totalBytes = 0).

## Notes

- The average speed is the **overall** average from start to finish
- It includes all retries and stalls in the calculation
- It reflects actual wall-clock time, not just active download time
- Differs from instantaneous speed shown during download progress
- More useful for overall performance assessment than peak speed

## Comparison with Instantaneous Speed

**Instantaneous Speed** (shown during progress):
- Current download rate at that moment
- Updates every 500ms
- Can vary significantly
- Shows: `Speed: 5.23 MB/s`

**Average Speed** (shown at completion):
- Total data / total time
- Calculated once at the end
- Smooths out variations
- Shows: `Average speed: 1.96 MB/s`

## Technical Details

### Data Types
- `QDateTime`: For timestamp precision
- `qint64`: For millisecond calculations (prevents overflow)
- `double`: For speed calculations (allows decimal precision)

### Precision
- Time: 2 decimal places (e.g., 125.34 seconds)
- Speed: 2 decimal places (e.g., 1.96 MB/s)
- Millisecond precision in calculations, displayed as seconds

### Edge Cases Handled
1. **Zero elapsed time**: Returns 0.0 MB/s (shouldn't happen in practice)
2. **Zero bytes downloaded**: Returns 0.0 MB/s (all files cached)
3. **Very short downloads**: Still calculates correctly (millisecond precision)
4. **Very long downloads**: qint64 prevents overflow (max ~292 million years)
