# 라즈베리파이 4 (AArch64) C/C++ 크로스 컴파일 템플릿

이 저장소는 Windows 호스트 환경에서 라즈베리파이 4(ARM64 / AArch64)용 C/C++ 애플리케이션을 빌드할 수 있는 Docker 및 CMake 기반의 크로스 컴파일 개발 환경을 제공합니다.

---

## 사전 요구 사항

* **호스트 PC:** Windows 10/11 ([Docker Desktop](https://www.docker.com/products/docker-desktop/) 설치 및 실행 필수)
* **타겟 기기:** 라즈베리파이 4 (64-bit OS 실행 중, SSH 접속 설정 완료)
* **터미널:** Windows PowerShell 또는 Git Bash

---

## 빌드 및 실행 순서

### 1. Docker 크로스 컴파일 이미지 빌드 (최초 1회 실행)

프로젝트 루트 폴더(`C:\raspberrypi`)에서 아래 명령어를 실행하여 `g++-aarch64-linux-gnu` 툴체인이 포함된 빌드용 도커 이미지를 생성합니다.

```bash
docker buildx build --platform linux/amd64 -t mep03-cross:amd64 --load .
```

---

### 2. 크로스 컴파일 실행 (실행 파일 생성)

출력 폴더를 생성하고 Docker 컨테이너를 구동하여 라즈베리파이 전용 바이너리를 빌드합니다.

**PowerShell 환경:**
```powershell
# 1. 출력 결과물 폴더 생성
mkdir -p build-docker-amd64

# 2. 크로스 컴파일 실행
docker run --rm --platform linux/amd64 -v "${PWD}:/src:ro" -v "${PWD}/build-docker-amd64:/out" mep03-cross:amd64
```

**Git Bash / Linux 환경:**
```bash
mkdir -p build-docker-amd64
docker run --rm --platform linux/amd64 -v "$PWD:/src:ro" -v "$PWD/build-docker-amd64:/out" mep03-cross:amd64
```

---

### 3. 생성된 바이너리 검증 (선택 사항)

빌드된 실행 파일이 ARM 64-bit(`aarch64`) 용이 맞는지 확인합니다.

```powershell
docker run --rm --platform linux/amd64 -v "${PWD}/build-docker-amd64:/out:ro" mep03-cross:amd64 file /out/hello_pi
```
* **정상 출력 예시:** `ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV)...`

---

### 4. 라즈베리파이 전송 및 실행

SCP를 사용해 실행 파일을 라즈베리파이로 전송하고 SSH로 실행 권한을 부여하여 실행합니다.

```powershell
# 1. 라즈베리파이 홈 디렉터리로 파일 전송
scp build-docker-amd64/hello_pi admin@10.66.73.174:~/

# 2. 실행 권한 부여 및 프로그램 실행
ssh admin@10.66.73.174 'chmod +x ~/hello_pi && ~/hello_pi'
```

---

## 일상 개발 워크플로우 (Quick Guide)

`src/` 디렉터리의 소스 코드를 수정한 뒤에는 아래 **2줄 명령어**만 실행하시면 됩니다.

```powershell
# 1단계: 재컴파일
docker run --rm --platform linux/amd64 -v "${PWD}:/src:ro" -v "${PWD}/build-docker-amd64:/out" mep03-cross:amd64

# 2단계: 전송 및 바로 실행
scp build-docker-amd64/hello_pi admin@10.66.73.174:~/ ; ssh admin@10.66.73.174 'chmod +x ~/hello_pi && ~/hello_pi'
```
