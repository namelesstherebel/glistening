# Pinned software OpenGL for hosted CI VMs without a GPU. Never installed system-wide.
$ErrorActionPreference = 'Stop'
$archive = Join-Path $env:RUNNER_TEMP 'mesa3d-26.2.4-release-msvc.7z'
$destination = Join-Path $env:RUNNER_TEMP 'glistening-mesa'
Invoke-WebRequest 'https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z' -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695') {
    throw 'Mesa SHA-256 mismatch'
}
& 7z x $archive "-o$destination" -y
if ($LASTEXITCODE -ne 0) { throw 'Mesa extraction failed' }
$dlls = @('opengl32.dll', 'libgallium_wgl.dll')
$executableDirectories = Get-ChildItem build -Recurse -Filter '*.exe' | ForEach-Object { $_.DirectoryName } | Sort-Object -Unique
foreach ($directory in $executableDirectories) {
    foreach ($dll in $dlls) { Copy-Item (Join-Path $destination "x64/$dll") $directory }
}
