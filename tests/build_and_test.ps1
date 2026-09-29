# PowerShell build script for Windows
# Compiles and runs Person 3 & 4 tests

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  Building Person 3 & 4 Detection Modules" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# Create build directory
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# Compile deadlock detector
Write-Host "Compiling deadlock_detector.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c ../src/detection/deadlock_detector.c -o build/deadlock_detector.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

# Compile test file
Write-Host "Compiling test_deadlock_detector.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c test_deadlock_detector.c -o build/test_deadlock_detector.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

# Link deadlock detector test
Write-Host "Linking deadlock test..." -ForegroundColor Yellow
gcc build/test_deadlock_detector.o build/deadlock_detector.o -o build/test_deadlock_detector.exe

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Linking failed!" -ForegroundColor Red
    exit 1
}

# Compile PI detector
Write-Host "Compiling pi_detector.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c ../src/detection/pi_detector.c -o build/pi_detector.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

# Compile PI detector test file
Write-Host "Compiling test_pi_detector.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c test_pi_detector.c -o build/test_pi_detector.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

# Link PI detector test
Write-Host "Linking PI detector test..." -ForegroundColor Yellow
gcc build/test_pi_detector.o build/pi_detector.o -o build/test_pi_detector.exe

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Linking failed!" -ForegroundColor Red
    exit 1
}

# Compile remediation modules
Write-Host "Compiling lock_order_advisor.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c ../src/remediation/lock_order_advisor.c -o build/lock_order_advisor.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Compiling priority_boost.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c ../src/remediation/priority_boost.c -o build/priority_boost.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

# Compile remediation test file
Write-Host "Compiling test_remediation.c..." -ForegroundColor Yellow
gcc -Wall -Wextra -std=c11 -I../src -I. -c test_remediation.c -o build/test_remediation.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Compilation failed!" -ForegroundColor Red
    exit 1
}

# Link remediation test (needs deadlock_detector and pi_detector too)
Write-Host "Linking remediation test..." -ForegroundColor Yellow
gcc build/test_remediation.o build/lock_order_advisor.o build/priority_boost.o build/deadlock_detector.o build/pi_detector.o -o build/test_remediation.exe

if ($LASTEXITCODE -ne 0) {
    Write-Host "X Linking failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Build successful!" -ForegroundColor Green
Write-Host ""

# Run tests
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  Running Tests" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

Write-Host "--- Deadlock Detector Tests ---" -ForegroundColor Yellow
.\build\test_deadlock_detector.exe
$deadlockResult = $LASTEXITCODE

Write-Host ""
Write-Host "--- Priority Inversion Detector Tests ---" -ForegroundColor Yellow
.\build\test_pi_detector.exe
$piResult = $LASTEXITCODE

Write-Host ""
Write-Host "--- Remediation Module Tests ---" -ForegroundColor Yellow
.\build\test_remediation.exe
$remediationResult = $LASTEXITCODE

Write-Host ""
if ($deadlockResult -eq 0 -and $piResult -eq 0 -and $remediationResult -eq 0) {
    Write-Host "All tests passed!" -ForegroundColor Green
}
else {
    Write-Host "Some tests failed!" -ForegroundColor Red
    if ($deadlockResult -ne 0) {
        Write-Host "  - Deadlock detector tests failed" -ForegroundColor Red
    }
    if ($piResult -ne 0) {
        Write-Host "  - PI detector tests failed" -ForegroundColor Red
    }
    if ($remediationResult -ne 0) {
        Write-Host "  - Remediation tests failed" -ForegroundColor Red
    }
    exit 1
}

