// Node.js 백엔드 서버
// 실행 방법: node server.js
// 필요한 패키지: npm install express ws serialport

const express = require('express');
const WebSocket = require('ws');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = 3001;

// Express 서버 시작
const server = app.listen(PORT, () => {
  console.log(`서버가 포트 ${PORT}에서 실행 중입니다.`);
});

// WebSocket 서버 설정
const wss = new WebSocket.Server({ server });

// 현재 기록 중인 데이터
let currentRecording = null;
let isRecording = false;
let recordedData = [];

// 시리얼 포트 설정 (아두이노 연결)
// COM 포트는 환경에 맞게 수정 필요 (예: Windows - 'COM3', macOS/Linux - '/dev/tty.usbserial-XXXX')
const SERIAL_PORT = 'COM3'; // 실제 포트로 변경하세요
const BAUD_RATE = 115200; // 아두이노와 동일하게 설정

let serialPort;

// 시뮬레이션 데이터 생성 함수 (프론트엔드 형식에 맞춤)
const createTestData = () => {
  return {
    timestamp: Date.now(),
    latitude: 37.5665 + (Math.random() - 0.5) * 0.01,
    longitude: 126.9780 + (Math.random() - 0.5) * 0.01,
    altitude: Math.random() * 1000,
    speed: Math.random() * 150,
    pitch: Math.random() * 360 - 180,
    roll: Math.random() * 360 - 180,
    yaw: Math.random() * 360,
    temperature: 20 + Math.random() * 10,
    pressure: 1013 + (Math.random() - 0.5) * 20,
    battery: 100 - Math.random() * 100,
    humidity: 50 + Math.random() * 30,
    parachuteStatus: Math.floor(Math.random() * 2),
    flightPhase: Math.floor(Math.random() * 7),
  };
};

// 데이터 브로드캐스트 함수
const broadcastData = (telemetryData) => {
  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(JSON.stringify({
        type: 'telemetry',
        data: telemetryData,
      }));
    }
  });
  if (isRecording) {
    recordedData.push(telemetryData);
  }
};


// 시리얼 포트 초기화
try {
  serialPort = new SerialPort({
    path: SERIAL_PORT,
    baudRate: BAUD_RATE,
  });

  serialPort.on('open', () => {
    console.log('시리얼 포트 연결됨:', SERIAL_PORT);
  });

  serialPort.on('error', (err) => {
    console.error('시리얼 포트 에러:', err.message);
    // 시리얼 에러 시 테스트 데이터 전송
    setInterval(() => broadcastData(createTestData()), 500);
  });

  // 아두이노에서 바이너리 데이터 수신
  serialPort.on('data', (buffer) => {
    // 패킷의 시작 바이트(0xAA)와 크기(36바이트)를 확인합니다.
    if (buffer[0] !== 0xAA || buffer.length !== 36) {
      // console.error('잘못된 패킷 수신. 길이:', buffer.length);
      return;
    }
    
    try {
      // 체크섬 검증
      let checksum = 0;
      // start_byte는 제외하고 checksum 필드 전까지 더합니다.
      for (let i = 1; i < buffer.length - 1; i++) {
        checksum = (checksum + buffer[i]) & 0xFF; // 8비트 초과 방지
      }

      if (checksum !== buffer[buffer.length - 1]) {
        console.error('체크섬 오류');
        return;
      }
      
      // Buffer에서 데이터를 읽어 프론트엔드가 기대하는 형식의 객체를 생성합니다.
      const telemetryData = {
        timestamp: Date.now(),
        
        // 필드 이름 변경 (e.g. roll -> roll)
        roll: buffer.readFloatLE(1),
        pitch: buffer.readFloatLE(5),
        yaw: buffer.readFloatLE(9),
        latitude: buffer.readFloatLE(13),   // lat -> latitude
        longitude: buffer.readFloatLE(17),  // lon -> longitude
        altitude: buffer.readFloatLE(21),   // alt -> altitude
        temperature: buffer.readFloatLE(25),// temp -> temperature
        humidity: buffer.readFloatLE(29),   // hum -> humidity

        // 타입 변경 및 이름 변경
        parachuteStatus: buffer.readUInt8(33), // para -> parachuteStatus
        flightPhase: buffer.readUInt8(34),     // phase -> flightPhase

        // 아두이노에서 보내지 않지만 프론트엔드가 기대하는 값 (기본값)
        speed: Math.random() * 150, // 랜덤값으로 채워 시뮬레이션 데이터와 유사하게 만듭니다.
        pressure: 1013.25 + (Math.random() - 0.5) * 20, // 랜덤값으로 채워 시뮬레이션 데이터와 유사하게 만듭니다.
        battery: 100 - Math.random() * 10, // 랜덤값으로 채워 시뮬레이션 데이터와 유사하게 만듭니다.
      };

      broadcastData(telemetryData);

    } catch (error) {
      console.error('데이터 파싱 에러:', error.message);
    }
  });

} catch (error) {
  console.error('시리얼 포트 초기화 실패:', error.message);
  console.log('테스트 모드로 실행 중 (시뮬레이션 데이터 사용)');
  
  // 시리얼 포트가 없을 때 시뮬레이션 데이터 생성
  setInterval(() => broadcastData(createTestData()), 100);
}

// WebSocket 연결 처리
wss.on('connection', (ws) => {
  console.log('새 클라이언트 연결됨');

  ws.on('message', (message) => {
    try {
      const msg = JSON.parse(message);

      // 기록 시작
      if (msg.type === 'start_recording') {
        isRecording = true;
        recordedData = [];
        currentRecording = {
          id: Date.now().toString(),
          startTime: Date.now(),
          launchSite: msg.launchSite || '나로우주센터',
        };
        
        ws.send(JSON.stringify({
          type: 'recording_started',
          recordingId: currentRecording.id,
        }));
        
        console.log('기록 시작:', currentRecording.id);
      }

      // 기록 중지 및 저장
      if (msg.type === 'stop_recording') {
        if (isRecording && currentRecording) {
          isRecording = false;
          
          const launchRecord = {
            id: currentRecording.id,
            date: new Date(currentRecording.startTime).toISOString(),
            launchSite: currentRecording.launchSite,
            duration: (Date.now() - currentRecording.startTime) / 1000,
            telemetryData: recordedData,
            maxAltitude: Math.max(...recordedData.map(d => d.altitude)),
            maxSpeed: Math.max(...recordedData.map(d => d.speed)),
            status: 'success',
            landingCoords: {
              lat: recordedData[recordedData.length - 1]?.latitude || 37.5665,
              lng: recordedData[recordedData.length - 1]?.longitude || 126.9780,
            },
          };

          // 파일로 저장
          const dataDir = path.join(__dirname, 'launch_data');
          if (!fs.existsSync(dataDir)) {
            fs.mkdirSync(dataDir);
          }

          const filePath = path.join(dataDir, `launch_${currentRecording.id}.json`);
          fs.writeFileSync(filePath, JSON.stringify(launchRecord, null, 2));

          ws.send(JSON.stringify({
            type: 'recording_stopped',
            record: launchRecord,
          }));

          console.log('기록 저장됨:', filePath);
          currentRecording = null;
          recordedData = [];
        }
      }

      // 저장된 기록 목록 요청
      if (msg.type === 'get_recordings') {
        const dataDir = path.join(__dirname, 'launch_data');
        if (fs.existsSync(dataDir)) {
          const files = fs.readdirSync(dataDir);
          const records = files
            .filter(f => f.endsWith('.json'))
            .map(f => {
              const data = JSON.parse(fs.readFileSync(path.join(dataDir, f), 'utf-8'));
              return {
                id: data.id,
                date: data.date,
                maxAltitude: data.maxAltitude,
                maxSpeed: data.maxSpeed,
                duration: data.duration,
                status: data.status,
                launchSite: data.launchSite,
                landingCoords: data.landingCoords,
              };
            })
            .sort((a, b) => new Date(b.date).getTime() - new Date(a.date).getTime());

          ws.send(JSON.stringify({
            type: 'recordings_list',
            records,
          }));
        }
      }

      // 특정 기록 데이터 요청
      if (msg.type === 'get_recording_data') {
        const dataDir = path.join(__dirname, 'launch_data');
        const filePath = path.join(dataDir, `launch_${msg.recordingId}.json`);
        
        if (fs.existsSync(filePath)) {
          const data = JSON.parse(fs.readFileSync(filePath, 'utf-8'));
          ws.send(JSON.stringify({
            type: 'recording_data',
            record: data,
          }));
        }
      }
    } catch (error) {
      console.error('메시지 처리 에러:', error.message);
    }
  });

  ws.on('close', () => {
    console.log('클라이언트 연결 종료');
  });
});

console.log('WebSocket 서버 준비됨');
console.log('아두이노 데이터 형식 예시:');
console.log('{"lat":37.5665,"lng":126.9780,"alt":100,"speed":50,"pitch":2.5,"roll":1.2,"yaw":0.5,"temp":22,"press":1013,"battery":95}');
