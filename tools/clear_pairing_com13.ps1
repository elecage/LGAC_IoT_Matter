param(
    [string] $Port = "COM13",
    [string] $IdfPath
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")
Import-ProjectIdf -IdfPath $IdfPath

# Matter fabrics, Wi-Fi credentials, and commissioning state live in the NVS
# partition. ESP32-C3 ROM over USB Serial/JTAG cannot always run erase_flash,
# so erase only this partition when a clean pairing state is needed.
python (Join-Path $Script:RepoRoot ".esp-idf\components\esptool_py\esptool\esptool.py") `
    -p $Port `
    -b 460800 `
    --before default_reset `
    --after hard_reset `
    --chip esp32c3 `
    --no-stub `
    erase_region 0x9000 0x6000

if ($LASTEXITCODE -ne 0) {
    throw "NVS erase_region failed with exit code $LASTEXITCODE"
}
