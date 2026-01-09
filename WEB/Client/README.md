# 로켓 관제 웹

아두이노 센서 데이터를 실시간으로 받아 로켓 발사를 추적하고 기록하는 웹 애플리케이션입니다.

## 주요 기능

### 1. 메인 페이지 (실시간 추적)
- **Mapbox 3D 지도**: 로켓의 실시간 위치를 3D 지도에 표시
- **Three.js 기울기 시각화**: 로켓의 Pitch, Roll, Yaw를 3D 모델로 실시간 표시
- **발사 단계 추적**: 발사 준비 → 발사 → 상승 → 순항 → 하강 → 착륙
- **텔레메트리 데이터**: 고도, 속도, 온도, 기압, 배터리 등 실시간 데이터 표시
- **기록 기능**: "기록 시작" 버튼으로 발사 데이터를 저장

### 2. 로켓 회수 페이지
- Google Maps 기반 회수 지점 마커 관리
- 지도 클릭으로 마커 추가
- 각 마커에 메모 작성 가능
- 낙하 지점 위치 정보 저장

### 3. 발사 기록 페이지
- 과거 발사 기록 목록 조회
- 발사 통계 (총 발사 횟수, 성공률, 최고 고도/속도)
- 리플레이 기능: 발사 기록을 메인 페이지에서 재생
- 시간 조작, 속도 조절 (0.5x ~ 10x)

## 시스템 아키텍처

```
아두이노 (센서) 
    ↓ 시리얼 통신 (JSON)
Node.js 백엔드 (server.js)
    ↓ WebSocket
React 프론트엔드
```

## 설치 및 실행

### 1. 백엔드 서버 설정

```bash
# 의존성 설치
npm install

# 서버 실행
node server.js
```

서버는 `localhost:3001`에서 실행됩니다.

### 2. 시리얼 포트 설정

`server.js` 파일에서 아두이노 연결 포트를 수정하세요:

```javascript
const SERIAL_PORT = 'COM3'; // Windows
// const SERIAL_PORT = '/dev/ttyUSB0'; // Linux
// const SERIAL_PORT = '/dev/cu.usbmodem14201'; // macOS
```

### 3. 아두이노 설정

`arduino_example.ino` 파일을 참고하여 아두이노에서 JSON 형식으로 데이터를 전송하세요:

```json
{
  "lat": 37.5665,
  "lng": 126.9780,
  "alt": 100,
  "speed": 50,
  "pitch": 2.5,
  "roll": 1.2,
  "yaw": 0.5,
  "temp": 22,
  "press": 1013,
  "battery": 95
}
```

필요한 센서:
- GPS 모듈 (위치, 속도)
- IMU/자이로 센서 (pitch, roll, yaw)
- 기압계 (고도, 기압)
- 온도 센서
- 배터리 전압 모니터

## 데이터 저장

발사 기록은 `launch_data/` 디렉토리에 JSON 파일로 저장됩니다:

```
launch_data/
  ├── launch_1704678600000.json
  ├── launch_1704765000000.json
  └── ...
```

각 파일에는 전체 텔레메트리 데이터, 최대 고도/속도, 비행 시간 등이 포함됩니다.

## API 키 설정 (선택사항)

실제 지도를 사용하려면 API 키가 필요합니다:

1. **Mapbox API**: https://www.mapbox.com/
2. **Google Maps API**: https://developers.google.com/maps

현재는 시각적 플레이스홀더로 구현되어 있어 API 키 없이도 작동합니다.

## 기술 스택

### 프론트엔드
- React + TypeScript
- Tailwind CSS v4
- Three.js (3D 시각화)
- Recharts (차트)
- React Router (라우팅)
- WebSocket (실시간 통신)

### 백엔드
- Node.js
- Express
- WebSocket (ws)
- SerialPort (아두이노 통신)

## 트러블슈팅

### 백엔드 서버 연결 안됨
- `node server.js`로 서버가 실행 중인지 확인
- 포트 3001이 사용 가능한지 확인
- 방화벽 설정 확인

### 시리얼 포트 에러
- 아두이노가 올바른 포트에 연결되어 있는지 확인
- `SERIAL_PORT` 설정이 올바른지 확인
- 다른 프로그램이 시리얼 포트를 사용 중인지 확인

### 테스트 모드
시리얼 포트가 없어도 서버는 시뮬레이션 데이터로 작동합니다.
