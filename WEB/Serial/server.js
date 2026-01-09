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
// COM 포트는 환경에 맞게 수정 필요 (예: Windows - 'COM3', macOS/Linux - '/dev/ttyUSB0')
const SERIAL_PORT = 'COM3'; // 실제 포트로 변경하세요
const BAUD_RATE = 9600;

let serialPort;
let parser;

// 시리얼 포트 초기화
try {
  serialPort = new SerialPort({
    path: SERIAL_PORT,
    baudRate: BAUD_RATE,
  });

  parser = serialPort.pipe(new ReadlineParser({ delimiter: '\n' }));

  serialPort.on('open', () => {
    console.log('시리얼 포트 연결됨:', SERIAL_PORT);
  });

  serialPort.on('error', (err) => {
    console.error('시리얼 포트 에러:', err.message);
    setInterval(() => { //에러 나면 테스트코드 송출
      const telemetryData = {
        timestamp: Date.now(),
        latitude: 37.5665 + Math.random() * 0.001,
        longitude: 126.9780 + Math.random() * 0.001,
        altitude: Math.random() * 1000,
        speed: Math.random() * 100,
        pitch: Math.random() * 10 - 5,
        roll: Math.random() * 10 - 5,
        yaw: Math.random() * 5 - 2.5,
        temperature: 22 + Math.random() * 3,
        pressure: 1013 - Math.random() * 10,
        battery: 100 - Math.random() * 10,
      };
  
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
    }, 1000);
  });

  // 아두이노에서 데이터 수신
  parser.on('data', (line) => {
    try {
      // 아두이노에서 JSON 형식으로 데이터를 보낸다고 가정
      // 예: {"lat":37.5665,"lng":126.9780,"alt":100,"speed":50,"pitch":2.5,"roll":1.2,"yaw":0.5,"temp":22,"press":1013}
      const data = JSON.parse(line);
      
      const telemetryData = {
        timestamp: Date.now(),
        latitude: data.lat || 37.5665,
        longitude: data.lng || 126.9780,
        altitude: data.alt || 0,
        speed: data.speed || 0,
        pitch: data.pitch || 0,
        roll: data.roll || 0,
        yaw: data.yaw || 0,
        temperature: data.temp || 22,
        pressure: data.press || 1013,
        battery: data.battery || 100,
      };

      // 웹소켓으로 모든 클라이언트에게 전송
      wss.clients.forEach((client) => {
        console.log(telemetryData)
        if (client.readyState === WebSocket.OPEN) {
          client.send(JSON.stringify({
            type: 'telemetry',
            data: telemetryData,
          }));
        }
      });

      // 기록 중이면 데이터 저장
      if (isRecording) {
        recordedData.push(telemetryData);
      }
    } catch (error) {
      console.error('데이터 파싱 에러:', error.message);
    }
  });
} catch (error) {
  console.error('시리얼 포트 초기화 실패:', error.message);
  console.log('테스트 모드로 실행 중 (시뮬레이션 데이터 사용)');
  
  // 시리얼 포트가 없을 때 시뮬레이션 데이터 생성
  setInterval(() => {
    const telemetryData = {
      timestamp: Date.now(),
      latitude: 37.5665 + Math.random() * 0.001,
      longitude: 126.9780 + Math.random() * 0.001,
      altitude: Math.random() * 1000,
      speed: Math.random() * 100,
      pitch: Math.random() * 10 - 5,
      roll: Math.random() * 10 - 5,
      yaw: Math.random() * 5 - 2.5,
      temperature: 22 + Math.random() * 3,
      pressure: 1013 - Math.random() * 10,
      battery: 100 - Math.random() * 10,
    };

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
  }, 1000);
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
