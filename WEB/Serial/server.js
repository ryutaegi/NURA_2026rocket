// Node.js 백엔드 서버
// 실행 방법: node server.js
// 필요한 패키지: npm install express ws serialport

require('dotenv').config(); // .env 파일 로드를 위해 추가
const axios = require('axios'); // HTTP 요청을 위해 추가
const express = require('express');
const cors = require('cors'); // CORS 미들웨어 추가
const WebSocket = require('ws');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const fs = require('fs');
const path = require('path');

const app = express();
app.use(cors()); // CORS 미들웨어 적용
app.use(express.json()); // JSON 요청 본문을 파싱하기 위해 추가

// 다운로드를 위해 launch_data 디렉토리를 정적 파일로 제공
app.use('/launch-data', express.static(path.join(__dirname, 'launch_data')));
const PORT = 3001;

// Express 서버 시작
const server = app.listen(PORT, () => {
  console.log(`서버가 포트 ${PORT}에서 실행 중입니다.`);
});

// --- 복구 마커 API 엔드포인트 ---

// 저장된 복구 마커 파일 목록 가져오기
app.get('/api/recovery-markers', (req, res) => {
  const dataDir = path.join(__dirname, 'recovery_data');
  if (!fs.existsSync(dataDir)) {
    return res.json([]);
  }
  fs.readdir(dataDir, (err, files) => {
    if (err) {
      console.error("디렉토리 읽기 에러:", err);
      return res.status(500).json({ message: "서버에서 디렉토리를 읽는 중 오류가 발생했습니다." });
    }
    const jsonFiles = files
      .filter(f => f.endsWith('.json'))
      .sort((a, b) => b.localeCompare(a)); // 최신 파일이 위로 오도록 정렬
    res.json(jsonFiles);
  });
});

// 새 복구 마커 세트 저장하기
app.post('/api/recovery-markers', (req, res) => {
  const markers = req.body.markers;
  if (!markers || !Array.isArray(markers)) {
    return res.status(400).json({ message: "잘못된 마커 데이터 형식입니다." });
  }

  const dataDir = path.join(__dirname, 'recovery_data');
  if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir);
  }

  const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
  const filename = `recovery-markers-${timestamp}.json`;
  const filePath = path.join(dataDir, filename);

  fs.writeFile(filePath, JSON.stringify(markers, null, 2), (err) => {
    if (err) {
      console.error("파일 쓰기 에러:", err);
      return res.status(500).json({ message: "서버에서 파일을 저장하는 중 오류가 발생했습니다." });
    }
    res.status(201).json({ message: "마커가 성공적으로 저장되었습니다.", filename });
  });
});

// 특정 복구 마커 파일 내용 가져오기
app.get('/api/recovery-markers/:filename', (req, res) => {
  const { filename } = req.params;
  const dataDir = path.join(__dirname, 'recovery_data');
  const filePath = path.join(dataDir, filename);

  // 경로 조작 공격 방지
  if (path.dirname(filePath) !== dataDir) {
    return res.status(400).json({ message: "잘못된 파일 경로입니다." });
  }

  if (!fs.existsSync(filePath)) {
    return res.status(404).json({ message: "파일을 찾을 수 없습니다." });
  }

  fs.readFile(filePath, 'utf8', (err, data) => {
    if (err) {
      console.error("파일 읽기 에러:", err);
      return res.status(500).json({ message: "서버에서 파일을 읽는 중 오류가 발생했습니다." });
    }
    res.json(JSON.parse(data));
  });
});

// 특정 복구 마커 파일 삭제하기
app.delete('/api/recovery-markers/:filename', (req, res) => {
  const { filename } = req.params;

  const dataDir = path.join(__dirname, 'recovery_data');
  const filePath = path.join(dataDir, filename);
  console.log(`[DELETE] 확인 경로: ${filePath}`);

  // 경로 조작 공격 방지
  if (path.dirname(filePath) !== dataDir) {
    console.error(`[DELETE] 경로 조작 시도 감지됨: ${filename}`);
    return res.status(400).json({ message: "잘못된 파일 경로입니다." });
  }

  if (!fs.existsSync(filePath)) {
    console.error(`[DELETE] 파일을 찾을 수 없음: ${filePath}`);
    return res.status(404).json({ message: "파일을 찾을 수 없습니다." });
  }

  fs.unlink(filePath, (err) => {
    if (err) {
      console.error("파일 삭제 에러:", err);
      return res.status(500).json({ message: "서버에서 파일을 삭제하는 중 오류가 발생했습니다." });
    }
    console.log(`[DELETE] 파일 삭제 성공: ${filename}`);
    res.status(200).json({ message: "파일이 성공적으로 삭제되었습니다." });
  });
});

// --- 원격 제어 API 엔드포인트 ---

// 비상 사출 명령
app.post('/api/emergency-eject', (req, res) => {
  if (serialPort && serialPort.isOpen) {
    serialPort.write('EJECT\n', (err) => {
      if (err) {
        console.error('시리얼 쓰기 에러 (EJECT):', err.message);
        return res.status(500).json({ message: '명령 전송에 실패했습니다.' });
      }
      console.log('비상 사출 명령 전송됨');
      res.status(200).json({ message: '비상 사출 명령이 전송되었습니다.' });
    });
  } else {
    console.warn('비상 사출 명령 실패: 시리얼 포트가 연결되지 않음');
    res.status(503).json({ message: '지상국 수신기가 연결되지 않았습니다.' });
  }
});

// 중앙 정렬 명령
app.post('/api/center-align', (req, res) => {
  if (serialPort && serialPort.isOpen) {
    serialPort.write('CENTER\n', (err) => {
      if (err) {
        console.error('시리얼 쓰기 에러 (CENTER):', err.message);
        return res.status(500).json({ message: '명령 전송에 실패했습니다.' });
      }
      console.log('중앙 정렬 명령 전송됨');
      res.status(200).json({ message: '중앙 정렬 명령이 전송되었습니다.' });
    });
  } else {
    console.warn('중앙 정렬 명령 실패: 시리얼 포트가 연결되지 않음');
    res.status(503).json({ message: '지상국 수신기가 연결되지 않았습니다.' });
  }
});


// --- WebSocket 서버 설정 ---
const wss = new WebSocket.Server({ server });

// 현재 기록 중인 데이터
let currentRecording = null;
let isRecording = false;
let recordedData = [];

// 시리얼 포트 설정 (아두이노 연결)
// COM 포트는 환경에 맞게 수정 필요 (예: Windows - 'COM3', macOS/Linux - '/dev/tty.usbserial-XXXX')
const SERIAL_PORT = '/dev/tty.usbserial-110'; // 실제 포트로 변경하세요
const BAUD_RATE = 115200; // 아두이노와 동일하게 설정

let serialPort;

let rxBuffer = Buffer.alloc(0);
const EXPECTED_PACKET_LEN = 44;
const SYNC_BYTE = 0xAA;


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
    connect: Math.floor(Math.random() * 2),
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
  serialPort.on('data', (chunk) => {
    // 들어온 데이터를 누적
    rxBuffer = Buffer.concat([rxBuffer, chunk]);
    // 패킷 단위로 처리
    while (rxBuffer.length >= EXPECTED_PACKET_LEN) {
      // sync byte 찾기
      const syncIndex = rxBuffer.indexOf(SYNC_BYTE);
      if (syncIndex < 0) {
        // sync 못 찾으면 전부 버림
        rxBuffer = Buffer.alloc(0);
        return;
      }

      // sync 앞의 쓰레기 데이터 제거
      if (syncIndex > 0) {
        rxBuffer = rxBuffer.slice(syncIndex);
      }

      // 아직 패킷 하나 분량 안 되면 대기
      if (rxBuffer.length < EXPECTED_PACKET_LEN) {
        return;
      }

      // 패킷 하나 추출
      const packet = rxBuffer.slice(0, EXPECTED_PACKET_LEN);
      rxBuffer = rxBuffer.slice(EXPECTED_PACKET_LEN);

      // 체크섬 검증
      let checksum = 0;
      for (let i = 1; i < EXPECTED_PACKET_LEN - 1; i++) {
        checksum = (checksum + packet[i]) & 0xFF;
      }

      if (checksum !== packet[EXPECTED_PACKET_LEN - 1]) {
        console.error("체크섬 오류, 패킷 버림");
        continue; // 다음 패킷 탐색
      }

      try {
        const telemetryData = {
          timestamp: Date.now(),

          roll: packet.readFloatLE(1),
          pitch: packet.readFloatLE(5),
          yaw: packet.readFloatLE(9),
          latitude: packet.readFloatLE(13),
          longitude: packet.readFloatLE(17),
          altitude: packet.readFloatLE(21),
          temperature: packet.readFloatLE(25),
          connect: packet.readFloatLE(29),
          speed: packet.readFloatLE(33),
          pressure: packet.readFloatLE(37),
          parachuteStatus: packet.readUInt8(41),
          flightPhase: packet.readUInt8(42),
          battery: 100, // 임시
        };

        broadcastData(telemetryData);

      } catch (e) {
        console.error("패킷 파싱 에러:", e.message);
      }
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

  ws.on('message', async (message) => { // 메시지 핸들러를 async로 변경
    try {
      const msg = JSON.parse(message);

      // --- 웹소켓 제어 명령 처리 추가 ---
      if (msg.type === 'emergency_eject') {
        if (serialPort && serialPort.isOpen) {
          serialPort.write('EJECT\n', (err) => {
            if (err) {
              console.error('시리얼 쓰기 에러 (EJECT):', err.message);
              return ws.send(JSON.stringify({ type: 'error', message: '명령 전송에 실패했습니다.' }));
            }
            console.log('[WS] 비상 사출 명령 전송됨');
            ws.send(JSON.stringify({ type: 'command_success', message: '비상 사출 명령이 전송되었습니다.' }));
          });
        } else {
          ws.send(JSON.stringify({ type: 'error', message: '지상국 수신기가 연결되지 않았습니다.' }));
        }
      }

      if (msg.type === 'center_align') {
        if (serialPort && serialPort.isOpen) {
          serialPort.write('CENTER\n', (err) => {
            if (err) {
              console.error('시리얼 쓰기 에러 (CENTER):', err.message);
              return ws.send(JSON.stringify({ type: 'error', message: '명령 전송에 실패했습니다.' }));
            }
            console.log('[WS] 중앙 정렬 명령 전송됨');
            ws.send(JSON.stringify({ type: 'command_success', message: '카운트다운이 시작되었습니다.' }));
          });
        } else {
          ws.send(JSON.stringify({ type: 'error', message: '지상국 수신기가 연결되지 않았습니다.' }));
        }
      }

      // 기록 시작
      if (msg.type === 'start_recording') {
        let humanReadableAddress = '알 수 없음';
        const latLngString = msg.data.launchSite; // "latitude, longitude" 형식의 문자열
        console.log('리버스 지오코딩 요청 좌표:', latLngString); // 요청 좌표 로깅

        if (latLngString && process.env.GOOGLE_MAPS_API_KEY) {
          try {
            const response = await axios.get(`https://maps.googleapis.com/maps/api/geocode/json`, {
              params: {
                latlng: latLngString,
                key: process.env.GOOGLE_MAPS_API_KEY,
                language: 'ko' // 결과를 한국어로 받기
              }
            });
            console.log('Google Geocoding API 응답:', JSON.stringify(response.data, null, 2)); // API 응답 전체 로깅

            if (response.data.results && response.data.results.length > 0) {
              humanReadableAddress = response.data.results[0].formatted_address;
            } else {
              console.warn('Google Geocoding API: 주소를 찾을 수 없습니다.');
              humanReadableAddress = latLngString; // 주소를 찾지 못하면 좌표로 대체
            }
          } catch (error) {
            console.error('리버스 지오코딩 API 호출 중 에러 발생:', error.message);
            humanReadableAddress = latLngString; // 에러 발생 시 좌표로 대체
          }
        } else {
          humanReadableAddress = latLngString || '알 수 없음'; // API 키 없거나 좌표 없으면 기본값
        }

        isRecording = true;
        recordedData = [];
        currentRecording = {
          id: Date.now().toString(),
          startTime: Date.now(),
          launchSite: humanReadableAddress, // 리버스 지오코딩된 주소 저장
        };

        ws.send(JSON.stringify({
          type: 'recording_started',
          recordingId: currentRecording.id,
        }));

        console.log('기록 시작:', currentRecording.id, '장소:', humanReadableAddress);
      }

      // 기록 중지 및 저장
      if (msg.type === 'stop_recording') {
        if (isRecording && currentRecording) {
          isRecording = false;
          const msgData = msg.data || {}; // 메시지에 data 객체가 없을 경우를 대비

          const launchRecord = {
            id: currentRecording.id,
            name: msgData.name || `발사 #${currentRecording.id}`, // 요청: 메시지에서 이름(name)을 받아 추가
            date: new Date(currentRecording.startTime).toISOString(),
            launchSite: currentRecording.launchSite, // 'start_recording' 시점에 저장된 위치
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
                name: data.name, // "name" 필드 추가
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

      // 발사 기록 삭제 요청
      if (msg.type === 'delete_recording') {
        const { recordingId } = msg;
        // 간단한 경로 조작 방지
        if (!recordingId || String(recordingId).includes('..') || String(recordingId).includes('/')) {
          console.error('잘못된 recordingId:', recordingId);
          return ws.send(JSON.stringify({ type: 'error', message: '잘못된 기록 ID입니다.' }));
        }

        const dataDir = path.join(__dirname, 'launch_data');
        const filePath = path.join(dataDir, `launch_${recordingId}.json`);

        if (fs.existsSync(filePath)) {
          fs.unlink(filePath, (err) => {
            if (err) {
              console.error('파일 삭제 에러:', err);
              ws.send(JSON.stringify({ type: 'error', message: '파일 삭제 중 오류가 발생했습니다.' }));
            } else {
              console.log('파일 삭제 성공:', filePath);
              ws.send(JSON.stringify({ type: 'recording_deleted', recordingId }));
            }
          });
        } else {
          console.warn('삭제할 파일을 찾을 수 없음:', filePath);
          ws.send(JSON.stringify({ type: 'error', message: '삭제할 파일을 찾을 수 없습니다.' }));
        }
      }

      // 발사 기록 상태 업데이트 요청
      if (msg.type === 'update_launch_status') {
        const { recordingId, newStatus } = msg;
        if (!recordingId || String(recordingId).includes('..') || String(recordingId).includes('/')) {
          console.error('잘못된 recordingId:', recordingId);
          return ws.send(JSON.stringify({ type: 'error', message: '잘못된 기록 ID입니다.' }));
        }
        if (!['success', 'partial', 'failed'].includes(newStatus)) { // 'unknown'은 UI에서만 사용되므로 서버에서는 이 3가지만 처리
          console.error('잘못된 상태 값:', newStatus);
          return ws.send(JSON.stringify({ type: 'error', message: '잘못된 상태 값입니다.' }));
        }

        const dataDir = path.join(__dirname, 'launch_data');
        const filePath = path.join(dataDir, `launch_${recordingId}.json`);

        if (fs.existsSync(filePath)) {
          fs.readFile(filePath, 'utf8', (err, data) => {
            if (err) {
              console.error('파일 읽기 에러:', err);
              return ws.send(JSON.stringify({ type: 'error', message: '파일 읽기 중 오류가 발생했습니다.' }));
            }
            try {
              const launchRecord = JSON.parse(data);
              launchRecord.status = newStatus;

              fs.writeFile(filePath, JSON.stringify(launchRecord, null, 2), (err) => {
                if (err) {
                  console.error('파일 쓰기 에러:', err);
                  return ws.send(JSON.stringify({ type: 'error', message: '파일 저장 중 오류가 발생했습니다.' }));
                }
                console.log(`기록 ${recordingId}의 상태가 ${newStatus}로 업데이트되었습니다.`);
                ws.send(JSON.stringify({ type: 'launch_status_updated', recordingId, newStatus }));
              });
            } catch (parseError) {
              console.error('JSON 파싱 에러:', parseError);
              ws.send(JSON.stringify({ type: 'error', message: '파일 파싱 중 오류가 발생했습니다.' }));
            }
          });
        } else {
          console.warn('상태를 업데이트할 파일을 찾을 수 없음:', filePath);
          ws.send(JSON.stringify({ type: 'error', message: '업데이트할 파일을 찾을 수 없습니다.' }));
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
