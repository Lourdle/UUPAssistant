Set-Location -Path $PSScriptRoot
$adk_path_file = "..\deps\adk_path.txt"
$possible_paths = @(
    "C:\Program Files (x86)\Windows Kits\10\Assessment and Deployment Kit\Deployment Tools",
    "C:\Program Files\Windows Kits\10\Assessment and Deployment Kit\Deployment Tools"
)
$needed_files = @(
    "SDKs\DismApi\Include\dismapi.h",
    "SDKs\DismApi\Lib\amd64\dismapi.lib",
    "SDKs\DismApi\Lib\arm64\dismapi.lib",
    "SDKs\Wimgapi\Include\wimgapi.h",
    "SDKs\Wimgapi\Lib\amd64\wimgapi.lib",
    "SDKs\Wimgapi\Lib\arm64\wimgapi.lib",
    "amd64\Oscdimg\oscdimg.exe",
    "arm64\Oscdimg\oscdimg.exe"
)

if (Test-Path "..\deps\adk") {
    exit 0
}

if (Test-Path $adk_path_file) {
    $adk_path = Get-Content $adk_path_file | Select-Object -First 1
    Write-Host "Using existing ADK path from adk_path.txt: $adk_path"
} else {
    foreach ($path in $possible_paths) {
        if (Test-Path $path) {
            $adk_path = $path

            Set-Content -Path $adk_path_file -Value $adk_path
            Write-Host "Found ADK path: $adk_path"
            break
        }
    }
}

if (-not $adk_path -or -not (Test-Path $adk_path)) {
    Write-Host "Error: ADK deployment tools not found. Please install the Windows ADK Deployment Tools. See https://docs.microsoft.com/windows-hardware/get-started/adk-install for more information. If you have already installed the deployment tools, please create a file at 'deps\adk_path.txt' containing the path to the deployment tools directory."
    exit 1
}

Write-Host "Checking if the ADK path is valid..."
foreach ($file in $needed_files) {
    $full_path = Join-Path $adk_path $file
    if (-not (Test-Path $full_path)) {
        Write-Error "Required file not found: $full_path. Please ensure the ADK is properly installed."
        exit 1
    }
}

Write-Host "All required files found in ADK path."
..\bin\tools\MakeSymLink.exe "..\deps\adk" $adk_path
Write-Host "Created symbolic link to ADK path at ..\deps\adk"
