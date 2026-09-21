$py   = "C:\Users\longyaosi\python-sdk\python3.13.2\python.exe"
$espy = "C:\Users\longyaosi\.platformio\packages\tool-esptoolpy\esptool.py"
$bdir = "C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
$arts = "$bdir\.wb_build\esp32s3"
$tool = "$bdir\tools"

& $py $espy --chip esp32s3 --port COM7 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 "$arts\bootloader.bin" 0x8000 "$arts\partitions.bin" 0x10000 "$arts\firmware.bin" 2>&1 | Out-File "$tool\flash_revert.log" -Encoding ascii
"FLASH_RC=$LASTEXITCODE"

Start-Sleep -Seconds 3

& $py "$tool\cap_log.py" 2>&1 | Out-File "$tool\cap_revert.log" -Encoding ascii
"CAP_DONE"
