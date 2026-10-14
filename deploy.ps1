# 사용법: .\deploy.ps1 <타겟이름> [-PiHost admin@<pi주소>]
#   예)   .\deploy.ps1 hello_pi
#         .\deploy.ps1 bt_chat_server -PiHost admin@192.168.0.15
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$PiHost = "admin@raspberrypi.local"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot -replace '\\', '/'
$out  = "$root/build-docker-amd64"

# 1. Docker 툴체인으로 지정한 타겟만 크로스 컴파일
docker run --rm -v "${root}:/src" -v "${out}:/out" mep03-cross:amd64 sh -c `
    "cmake -S /src -B /out -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/aarch64-linux-gnu.cmake && cmake --build /out --target $Target"
if ($LASTEXITCODE -ne 0) { throw "빌드 실패: $Target" }

# 2. Pi 홈 폴더로 복사 후 실행 권한 부여
Push-Location $out
try {
    scp $Target "${PiHost}:~/"
    if ($LASTEXITCODE -ne 0) { throw "복사 실패: $Target" }
} finally {
    Pop-Location
}
ssh $PiHost "chmod +x ~/$Target"

Write-Host "완료: ${PiHost}:~/$Target"
