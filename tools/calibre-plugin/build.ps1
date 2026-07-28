$plugin_dir = "$PSScriptRoot"
$out        = "$plugin_dir\microreader.zip"
$install    = "$env:APPDATA\calibre\plugins\Microreader.zip"

# Build ZIP
Remove-Item -ErrorAction SilentlyContinue $out
$tmp = [System.IO.Path]::GetTempPath() + "mrd_plugin_" + [System.IO.Path]::GetRandomFileName()
New-Item -ItemType Directory -Path $tmp | Out-Null
Copy-Item "$plugin_dir\__init__.py" "$tmp\"
Copy-Item -Recurse "$plugin_dir\serial" "$tmp\serial"
Compress-Archive -Path "$tmp\*" -DestinationPath $out
Remove-Item -Recurse -Force $tmp
Write-Host "Built:     $out"

# Auto-install (Calibre must not be running)
Copy-Item -Force $out $install
Write-Host "Installed: $install"
