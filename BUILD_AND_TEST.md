# Build and Testing Guide

## Prerequisites

### Required Software
- **Qt 6.7.3** or compatible version (Qt 5.15+ may also work)
- **CMake 3.16+**
- **C++17 compatible compiler**:
  - Windows: MSVC 2019+ or MinGW with GCC 9+
  - Linux: GCC 9+ or Clang 10+
  - macOS: Clang from Xcode 12+

### Qt Installation
Download and install Qt from: https://www.qt.io/download

Make sure to install:
- Qt Core module
- Qt Network module

## Building the Project

### Windows (MSVC)

1. Open Qt Command Prompt or set up Qt environment:
```cmd
:: Set Qt path (adjust to your installation)
set Qt6_DIR=C:\Qt\6.7.3\msvc2019_64\lib\cmake\Qt6
set PATH=%PATH%;C:\Qt\6.7.3\msvc2019_64\bin
```

2. Build with CMake:
```cmd
cd C:\path\to\AsulDownloader
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

3. Run the executable:
```cmd
cd Release
AsulDownloader.exe
```

### Windows (MinGW)

1. Set up Qt environment:
```cmd
set Qt6_DIR=C:\Qt\6.7.3\mingw_64\lib\cmake\Qt6
set PATH=%PATH%;C:\Qt\6.7.3\mingw_64\bin;C:\Qt\Tools\mingw1120_64\bin
```

2. Build:
```cmd
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

### Linux

1. Install Qt development packages:
```bash
# Ubuntu/Debian
sudo apt-get install qt6-base-dev qt6-base-dev-tools

# Fedora
sudo dnf install qt6-qtbase-devel

# Arch Linux
sudo pacman -S qt6-base
```

2. Build:
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

3. Run:
```bash
./AsulDownloader
```

### macOS

1. Install Qt via Homebrew:
```bash
brew install qt@6
```

2. Build:
```bash
mkdir build
cd build
cmake -DQt6_DIR=/usr/local/opt/qt@6/lib/cmake/Qt6 ..
make -j$(sysctl -n hw.ncpu)
```

3. Run:
```bash
./AsulDownloader
```

## Testing the Optimizations

### Preparation

1. Place test JSON files in the same directory as the executable:
   - `assets.json` - Minecraft asset index
   - `version.json` - Minecraft version manifest

2. Ensure sufficient disk space (at least 1GB free for test downloads)

### Test Scenarios

#### Test 1: Small File Download (< 100 files)
**Purpose**: Verify basic functionality and speed calculation

1. Create a minimal `assets.json` with ~50-100 files
2. Run AsulDownloader
3. **Expected Results**:
   - All files download successfully
   - Speed display shows realistic values (1-50 MB/s depending on network)
   - No Windows handle errors
   - Application completes normally

#### Test 2: Medium Load (500-1000 files)
**Purpose**: Test resource management under moderate load

1. Use assets.json with 500-1000 files
2. Monitor system resources:
   - Open Task Manager → Details → AsulDownloader.exe
   - Watch "Handles" column (should stay under 2000)
3. **Expected Results**:
   - Steady handle count (not continuously increasing)
   - Consistent download speed
   - Progress updates every 500ms

#### Test 3: Full Load (4435 files)
**Purpose**: Verify fix for original issue

1. Use complete Minecraft assets.json (4435+ files)
2. Monitor throughout download:
```powershell
# PowerShell command to monitor handles
while ($true) { 
    Get-Process AsulDownloader | Select Name, Handles | Format-Table
    Start-Sleep -Seconds 5
}
```
3. **Expected Results**:
   - Download completes all 4435 files
   - Handle count stays under 3000 (vs. 10,000+ limit)
   - No "Window 管理器对象的系统允许的所有句柄" errors
   - Download speed remains stable (no 1000+ MB/s anomalies)
   - Application exits cleanly

### Monitoring Tips

#### Windows Task Manager
1. Add "Handles" column: View → Select Columns → Handles
2. Watch for:
   - Handle count < 3000 (good)
   - Handle count continuously rising (bad)

#### Resource Monitor
1. Open Resource Monitor (resmon.exe)
2. Go to CPU tab → Handles
3. Filter by AsulDownloader.exe
4. Check handle types:
   - Should see mostly File, Event, Section
   - NOT thousands of Timer or Window handles

#### Application Output
Watch for these indicators:
- `[PROGRESS]` lines show reasonable speeds (< 100 MB/s)
- Active downloads stay around 32
- No error messages about timers or windows

## Expected Performance Metrics

### Resource Usage
| Metric | Before Fix | After Fix | Improvement |
|--------|-----------|-----------|-------------|
| QNetworkAccessManager instances | ~4500+ | 8 | 99.8% reduction |
| Windows Handles | 10,000+ (exhausted) | < 3000 | < 30% of limit |
| Monitor callbacks/sec | 50 | 1 | 98% reduction |
| Max concurrent downloads | 16 | 32 | 100% increase |

### Download Speed
- **Normal range**: 5-50 MB/s (depends on network and server)
- **Alert if**: Speed shows > 100 MB/s consistently (likely calculation error)
- **Alert if**: Speed frequently shows 0.00 MB/s (possible network issue)

### Completion Time
- **4435 files (~500MB)**: 
  - 10 MB/s network: ~50-70 seconds
  - 5 MB/s network: ~100-150 seconds
  - 1 MB/s network: ~500-600 seconds

## Troubleshooting

### Build Errors

**"Qt6Config.cmake not found"**
```bash
# Set Qt6_DIR explicitly
cmake -DQt6_DIR=/path/to/Qt/6.7.3/gcc_64/lib/cmake/Qt6 ..
```

**"C++17 required"**
```bash
# Ensure compiler supports C++17
cmake -DCMAKE_CXX_STANDARD=17 ..
```

### Runtime Errors

**"assets.json not found"**
- Ensure JSON files are in same directory as executable
- Check current working directory with `QDir::currentPath()` output

**Still getting handle exhaustion**
- Reduce max concurrent downloads: `setMaxConcurrentDownloads(16)`
- Check for other applications consuming handles
- Verify fix was applied (check git commit hash)

**Speed showing 0.00 MB/s**
- Wait at least 2 seconds for calculation to stabilize
- Check network connectivity
- Verify files are actually downloading (check minecraft/ directory)

## Configuration Tuning

### For Slower Networks
```cpp
downloader.setMaxConcurrentDownloads(16);  // Reduce from 32
downloader.setDownloadTimeout(60000);       // Increase timeout to 60s
```

### For Faster Networks
```cpp
downloader.setMaxConcurrentDownloads(64);  // Increase from 32
downloader.setMaxConnectionsPerHost(16);    // Increase from 8
```

### For Limited System Resources
```cpp
downloader.setMaxConcurrentDownloads(16);  // Lower concurrency
downloader.setSegmentCountForLargeFile(2); // Fewer segments
```

## Verification Checklist

After building and running, verify:

- [ ] Application builds without errors
- [ ] All dependencies are met (Qt modules found)
- [ ] Application starts and finds JSON files
- [ ] Download progress appears in console
- [ ] Speed values are realistic (< 100 MB/s)
- [ ] Handle count stays under 3000 (Windows)
- [ ] All files download successfully
- [ ] Application exits cleanly (no crashes)
- [ ] Downloaded files are valid (correct sizes)

## Success Criteria

The optimization is successful if:

1. ✅ **Full download completes**: All 4435+ files download without errors
2. ✅ **No handle exhaustion**: Application runs without "Window 管理器对象的系统允许的所有句柄" errors
3. ✅ **Accurate speed reporting**: Download speeds are realistic and stable
4. ✅ **Improved performance**: Downloads complete faster than before due to increased concurrency
5. ✅ **Clean termination**: Application exits normally after completion

## Support

If you encounter issues:

1. Check this guide's troubleshooting section
2. Review OPTIMIZATION_SUMMARY.md for implementation details
3. Verify Qt version compatibility
4. Check system resources (RAM, handles, network)
5. Open an issue on GitHub with:
   - Build environment details
   - Error messages
   - Output logs
   - Handle count screenshots
