# LGAC IoT Matter

ESP32-C3 Super Mini 기반 LG 휘센 시스템에어컨 IR 리모컨 브리지 펌웨어입니다.
Google Home에는 Matter Thermostat 장치로 등록되고, Google Home에서 바뀐 명령을
LG 에어컨 IR 리모컨 프레임으로 변환해 송신합니다.

## 지원 명령

- 온도 조절: 냉방/난방 설정온도 변경을 IR 온도 명령으로 전송
- 냉방: Matter `SystemMode = Cool`을 LG 냉방 명령으로 전송
- 난방: Matter `SystemMode = Heat`를 LG 난방 명령으로 전송
- 송풍: Matter `SystemMode = FanOnly`를 LG 송풍 명령으로 전송
- 온: 냉방/난방/송풍/제습 등 Off가 아닌 운전 모드 선택 시 전원 켜짐 상태로 IR 전송
- 오프: Matter `SystemMode = Off`를 LG 전원 끄기 명령으로 전송
- 바람세기 조절: Matter `FanControl.FanMode`를 LG 풍량 Auto/Low/Medium/High 계열로 변환

Google Home UI가 어떤 컨트롤을 노출하는지는 Google Home 버전과 Matter thermostat 지원 상태에
따라 조금 달라질 수 있습니다. 펌웨어 쪽은 Thermostat cluster와 Fan Control cluster를 모두
받아 처리하도록 작성되어 있습니다.

## 하드웨어

- Board: ESP32-C3 Super Mini
- IR TX: 기본 GPIO4
- Serial port: COM13
- IR LED는 GPIO에 직접 연결하지 말고 NPN/N-MOSFET 트랜지스터 드라이버와 전류 제한 저항을 사용하세요.

IR GPIO는 `idf.py menuconfig`의 `LG Whisen IR AC > IR transmitter GPIO`에서 바꿀 수 있습니다.

## 개발 환경

이 저장소의 Python 도구는 `.venv`를 사용합니다.

```powershell
.\tools\setup_venv.ps1
```

Matter 빌드는 ESP-IDF와 ESP-Matter가 필요합니다. 이 프로젝트는 ESP Component Registry의
`espressif/esp_matter^1.4.2~1`을 사용하며, 해당 릴리스 권장 환경인 ESP-IDF v5.4.1을
기준으로 잡았습니다. ESP32-C3는 Wi-Fi Matter 디바이스로 사용할 수 있습니다.

ESP-IDF가 아직 없다면 저장소 내부 `.esp-idf` 폴더에 설치할 수 있습니다.

```powershell
.\tools\install_esp_idf.ps1
```

설치, venv 준비, ESP32-C3 타깃 설정을 한 번에 하려면:

```powershell
.\tools\bootstrap_env.ps1 -InstallEspIdf
```

이미 다른 위치에 ESP-IDF v5.4.1이 설치되어 있다면 `-IdfPath`로 지정할 수 있습니다.

```powershell
.\tools\bootstrap_env.ps1 -IdfPath "C:\Espressif\frameworks\esp-idf-v5.4.1"
```

빌드합니다.

```powershell
.\tools\build.ps1
```

필요하면 빌드할 때도 ESP-IDF 경로를 직접 지정할 수 있습니다.

```powershell
.\tools\build.ps1 -IdfPath "C:\Espressif\frameworks\esp-idf-v5.4.1"
```

## 플래시

보드가 `COM13`에 연결되어 있으면:

```powershell
.\tools\flash_com13.ps1
```

다른 포트를 쓰려면:

```powershell
.\tools\flash_com13.ps1 -Port COM7
```

## Google Home 페어링

펌웨어가 처음 부팅되면 Matter BLE commissioning이 열립니다. 로그에 출력되는 QR/manual pairing
정보를 Google Home 앱에서 사용해 등록합니다. 등록 후에는 Thermostat 장치로 보입니다.

## LG IR 프로토콜 주의점

`main/lg_ir.cpp`는 널리 쓰이는 LG 28-bit AC IR 프레임 형태를 기본 구현으로 넣었습니다.
LG 휘센 시스템에어컨은 리모컨/실내기 모델에 따라 바이트 배치나 특수 명령이 달라질 수 있습니다.
실제 장비가 반응하지 않으면 IR 수신 모듈로 원래 리모컨의 프레임을 캡처한 뒤
`build_lg_28bit_frame()`의 필드 배치를 맞춰야 합니다.
