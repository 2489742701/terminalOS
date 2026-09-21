$py = "C:\Users\longyaosi\python-sdk\python3.13.2\python.exe"
$bdir = "C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
$arts = "$bdir\.wb_build\esp32s3"
$espy = "C:\Users\longyaosi\.platformio\packages\tool-esptoolpy\esptool.py"
$log = "$bdir\tools"

$flashArgs = @(
  $espy
  "--chip","esp32s3"
  "--port","COM7"
  "--baud","921600"
  "--before","default_reset"
  "--after","hard_reset"
  "write_flash","-z"
  "--flash_mode","dio"
  "--flash_freq","80m"
  "--flash_size","16MB"
  "0x0","$arts\bootloader.bin"
  "0x8000","$arts\partitions.bin"
  "0x10000","$arts\firmware.bin"
)
Write-Host "=== FLASHING REAL APP (dio, opi psram) ==="
& $py @flashArgs 2>&1 | ForEach-Object { $_ }
Write-Host "FLASH_RC=$LASTEXITCODE"

# 端口释放后单独抓日志
Start-Sleep -Milliseconds 800
Write-Host "=== CAPTURING 18s ==="
& $py "$log\cap_log.py" 2>&1 | Set-Content -Path "$log\cap_real.log" -Encoding ascii
Write-Host "CAP DONE"
