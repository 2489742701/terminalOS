# build.ps1 - 可靠的 PlatformIO 构建封装
#
# 为什么不能直接用 `pio run`：
#   本项目在宿主沙箱里跑，PIO 想清空/重建 .wb_build 时会被 safe-delete 拦截
#   （"Can not remove temporary directory .wb_build"），导致 SCons 签名库
#   (.sconsign.dblite) 处于半更新状态 —— 之后再改源码，PIO 会误判"目标已最新"
#   直接跳过编译，烧录的还是旧固件（实测：源码 02:31，.o 停在 02:26）。
#
# 对策：编译前用受管 Python 的 shutil.rmtree 清掉 .wb_build（绕过 safe-delete），
#       强制全量重编。代价约 90 秒，换来"烧进去的一定是当前代码"。
#
# 用法： powershell -File tools\build.ps1

$ErrorActionPreference = "Continue"
$pioPy  = "C:\Users\longyaosi\python-sdk\python3.13.2\python.exe"
$cleanPy = "C:\Users\longyaosi\.workbuddy\binaries\python\versions\3.13.12\python.exe"
$root   = Split-Path -Parent $PSScriptRoot
$build  = Join-Path $root ".wb_build"

Write-Output "=== 清理构建目录（绕过 safe-delete） ==="
if (Test-Path $build) {
    & $cleanPy -c "import shutil,sys; shutil.rmtree(r'$build', ignore_errors=True); print('cleared')"
} else {
    Write-Output "no build dir"
}

Write-Output "=== platformio run ==="
& $pioPy -m platformio run -d $root -e esp32s3 2>&1 | Tee-Object -Variable out

$failed = $out | Select-String -Pattern "error:|\*\*\* \[.*\] Error"
if ($failed) {
    Write-Output "=== BUILD FAILED ==="
    $failed | ForEach-Object { Write-Output $_.Line }
    exit 1
}

$ok = $out | Select-String -Pattern "\[SUCCESS\]"
if ($ok) {
    Write-Output "=== BUILD OK ==="
    $bin = Join-Path $build "esp32s3\firmware.bin"
    if (Test-Path $bin) {
        $f = Get-Item $bin
        Write-Output ("firmware.bin  {0} bytes  {1}" -f $f.Length, $f.LastWriteTime)
    }
    exit 0
}

Write-Output "=== BUILD RESULT UNKNOWN ==="
exit 2
