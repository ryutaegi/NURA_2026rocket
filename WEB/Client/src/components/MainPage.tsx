import { useState, useEffect, useRef } from 'react';
import { useLocation } from 'react-router-dom';
import MapboxView from './MapboxView';
import RocketOrientation from './RocketOrientation';
import LaunchStages from './LaunchStages';
import RocketData from './RocketData';
import ReplayControls from './ReplayControls';
import { Activity, RotateCcw, Circle, Square, Radio, Signal, Share2 } from 'lucide-react';
import { useWebSocket } from '../hooks/useWebSocket';
import { Toaster, toast } from 'sonner';
import { db } from '../lib/firebase';
import { collection, addDoc, doc, setDoc, onSnapshot, serverTimestamp, Timestamp } from 'firebase/firestore';

export interface RocketTelemetry {
  // GPS
  latitude: number;   // lat / 1e7
  longitude: number;  // lon / 1e7
  altitude: number;   // alt (0~255m)

  // 쿼터니안 (Q15 디코딩 후 float)
  q0: number;
  q1: number;
  q2: number;
  q3: number;

  // 자세
  roll: number;   // 0~255 → -127~128°

  // 환경
  temperature: number;  // temp - 20 (-20~100°C)

  // flag1
  sats: number;         // 위성 개수 (bits 3:0)
  soundBtn: boolean;    // 소리버튼 (bit 4)
  ejectBtn: boolean;    // 사출버튼 (bit 5)
  parachute: boolean;   // 낙하산사출 (bit 6)
  connectPin: boolean;  // 커넥트핀 (bit 7)

  // flag2
  flightPhase: number;      // 발사단계 (bits 7:5)
  extra1: boolean;  // 예비 (bit 4)
  ejectDescent: boolean;    // 고도하강사출 (bit 3)
  ejectTimer: boolean;      // 시간지연사출 (bit 2)

  // derived
  stage: 'pre-launch' | 'launch' | 'powered' | 'coasting' | 'apogee' | 'descent' | 'landed';
  parachuteEjectReason: number; // 0: 없음, 1: 비상, 2: 고도, 3: 타이머
}

// 아두이노 FlightState enum에 따른 매핑 (STANDBY, POWERED, COASTING, APOGEE, DESCENT, LANDED)
export const flightPhaseToStageMap: { [key: number]: RocketTelemetry['stage'] } = {
  0: 'pre-launch',    // STANDBY
  1: 'launch',        // POWERED
  2: 'coasting',      // COASTING
  3: 'apogee',        // APOGEE
  4: 'descent',       // DESCENT
  5: 'landed',        // LANDED
};

// 원본 센서 데이터를 RocketTelemetry 형식으로 변환
export function convertRawTelemetry(data: any): RocketTelemetry {
  // 쿼터니안 Q15 디코딩
  const q1 = data.q1 / 32767.0;
  const q2 = data.q2 / 32767.0;
  const q3 = data.q3 / 32767.0;
  const q0 = Math.sqrt(Math.max(0, 1 - q1 ** 2 - q2 ** 2 - q3 ** 2));

  // flag1 파싱
  const connectPin = Boolean(data.flag1 & 0x01);
  const parachute  = Boolean((data.flag1 >> 1) & 0x01);
  const ejectBtn   = Boolean((data.flag1 >> 2) & 0x01);
  const soundBtn   = Boolean((data.flag1 >> 3) & 0x01);
  const sats       = (data.flag1 >> 4) & 0x0F;

  // flag2 파싱
  const flightPhase    = (data.flag2 >> 5) & 0x07;  // bits 7-5
  const ejectDescent   = Boolean((data.flag2 >> 3) & 0x01);  // bit 3
  const ejectTimer     = Boolean((data.flag2 >> 2) & 0x01);  // bit 2
  const extra1 = false;  // 사용 안 함

  // 낙하산 사출 이유
  const parachuteEjectReason = ejectBtn ? 1 : ejectDescent ? 2 : ejectTimer ? 3 : 0;

  // roll 스케일링
  const roll = data.roll - 127;

  return {
    latitude: data.lat / 1e7,
    longitude: data.lon / 1e7,
    altitude: data.alt,
    q0, q1, q2, q3,
    roll,
    temperature: data.temp - 20,
    sats,
    soundBtn,
    ejectBtn,
    parachute,
    connectPin,
    flightPhase,
    extra1,
    ejectDescent,
    ejectTimer,
    stage: flightPhaseToStageMap[flightPhase] || 'pre-launch',
    parachuteEjectReason,
  };
}

interface MainPageProps {
  centerAlign: boolean;
  emergencyEjection: boolean;
}

export default function MainPage({ centerAlign, emergencyEjection }: MainPageProps) {
  const location = useLocation();
  const replayLaunch = (location.state as any)?.replayLaunch;
  const { isConnected, lastMessage, sendMessage } = useWebSocket();
  const audioRef = useRef<HTMLAudioElement>(null);
  const [unlocked, setUnlocked] = useState(false);

  const [telemetry, setTelemetry] = useState<RocketTelemetry>({
    latitude: 37.5665,
    longitude: 126.9780,
    altitude: 0,
    q0: 1, q1: 0, q2: 0, q3: 0,
    roll: 0,
    temperature: 0,
    sats: 0,
    soundBtn: false,
    ejectBtn: false,
    parachute: false,
    connectPin: false,
    flightPhase: 0,
    extra1: false,
    ejectDescent: false,
    ejectTimer: false,
    stage: 'pre-launch',
    parachuteEjectReason: 0,
  });

  const [isRecording, setIsRecording] = useState(false);
  const [isReplayMode, setIsReplayMode] = useState(false);
  const [replayTime, setReplayTime] = useState(0);
  const [isReplayPlaying, setIsReplayPlaying] = useState(false);
  const [isBroadcasting, setIsBroadcasting] = useState(false);
  const [remoteData, setRemoteData] = useState<any>(null);
  const lastUploadTimeRef = useRef<number>(0);
  const prevIsConnectedRef = useRef<boolean>(false);
  const BROADCAST_INTERVAL_MS = 500; // 0.5초마다 업로드 (초당 2회)
  const [replaySpeed, setReplaySpeed] = useState(1);
  const [replayData, setReplayData] = useState<any>(null);
  const [showConnectedBanner, setShowConnectedBanner] = useState(false);
  const recordingStartTime = useRef<number>(0);
  const [rollHistory, setRollHistory] = useState<{ t: number; roll: number }[]>([]);
  const rollHistoryRef = useRef<{ t: number; roll: number }[]>([]);
  const ROLL_HISTORY_MAX = 100;

  const unlockAudio = async () => {
    const audio = audioRef.current;
    if (!audio) {
      console.error("❌ audioRef가 null입니다");
      return;
    }

    try {
      console.log("▶️ 오디오 언락 시도");

      audio.muted = false;
      audio.currentTime = 0;

      await audio.play();   // 여기서 실패하면 catch로 갑니다

      console.log("✅ play() 성공");
      setUnlocked(true);
    } catch (e) {
      console.error("❌ 오디오 재생 실패:", e);
      alert("오디오 재생 실패: 콘솔(F12) 확인하세요");
    }
  };


  // const playLater = () => {
  //   if (audioRef.current) {
  //     audioRef.current.play(); // 이제는 언제든지 재생 가능
  //   }
  // };

  const playSound = (src: string) => {
    if (!unlocked) {
      console.warn("오디오가 아직 언락되지 않았습니다.");
      return;
    }
  
    const audio = new Audio(src);
    audio.play().catch((e) => {
      console.error("사운드 재생 실패:", e);
    });
  };

  // 연결 상태 토스트 알림 (상태 변화 시 1회만)
  useEffect(() => {
    // 초기 로딩 시 현재 상태 알림
    if (prevIsConnectedRef.current === undefined) {
      if (isConnected) {
        toast.success("로컬 서버와 연결된 상태입니다.");
      } else {
        toast.info("로컬 서버와 연결되지 않았습니다.");
      }
      prevIsConnectedRef.current = isConnected;
      return;
    }

    if (isConnected !== prevIsConnectedRef.current) {
      if (isConnected) {
        toast.success("로컬 서버와 연결되었습니다.");
      } else {
        toast.error("로컬 서버와 연결이 끊어졌습니다.");
      }
      prevIsConnectedRef.current = isConnected;
    }
  }, [isConnected]);

  // Firebase 실시간 중계 문서 구독 (웹소켓 연결 안 되었을 때만)
  useEffect(() => {
    if (!isConnected) {
      // serverTimestamps: 'estimate'를 사용하여 null 인 상황 방지
      const unsub = onSnapshot(doc(db, "live", "current"), { includeMetadataChanges: true }, (snapshot) => {
        if (snapshot.exists()) {
          const data = snapshot.data({ serverTimestamps: 'estimate' });

          // 다양한 형태의 타임스탬프 처리
          let updatedAt = 0;
          if (data.serverTimestamp?.toDate) {
            updatedAt = data.serverTimestamp.toDate().getTime();
          } else if (data.serverTimestamp?.seconds) {
            updatedAt = data.serverTimestamp.seconds * 1000;
          } else if (typeof data.serverTimestamp === 'number') {
            updatedAt = data.serverTimestamp;
          } else if (data.telemetry?.timestamp) {
            // 서버 타임스탬프가 없으면 텔레메트리 자체 타임스탬프 사용 (차선책)
            updatedAt = data.telemetry.timestamp;
          }

          const timeDiff = Math.abs(Date.now() - updatedAt);
          console.log(`[Remote] Data received. UpdatedAt: ${updatedAt}, Diff: ${timeDiff}ms`);

          // 실시간 중계 데이터가 유효한지 확인 (60초 이내)
          if (updatedAt && timeDiff < 60000) {
            setRemoteData(data.telemetry);

            // 텔레메트리 상태 동기화 (대시보드 UI 갱신을 위해 필수)
            setTelemetry({
              ...data.telemetry,
              stage: flightPhaseToStageMap[data.telemetry.flightPhase] || 'pre-launch'
            });
          } else {
            if (updatedAt) console.warn("원격 데이터가 너무 오래되었습니다:", new Date(updatedAt).toLocaleString());
            setRemoteData(null);
          }
        }
      }, (err) => console.error("Remote Subscription Error:", err));
      return () => unsub();
    } else {
      setRemoteData(null);
    }
  }, [isConnected]);

  // 웹소켓 데이터를 파이어베이스로 실시간 중계
  useEffect(() => {
    if (isConnected && lastMessage?.type === 'telemetry' && isBroadcasting) {
      const now = Date.now();
      if (now - lastUploadTimeRef.current >= BROADCAST_INTERVAL_MS) {
        setDoc(doc(db, "live", "current"), {
          telemetry: lastMessage.data,
          serverTimestamp: serverTimestamp(), // 정확한 시간 동기화를 위해 서버 타임스탬프 사용
          broadcaster: "Ground Station"
        }, { merge: true }).catch(err => console.error("Broadcast Error:", err));
        lastUploadTimeRef.current = now;
      }
    }
  }, [isConnected, lastMessage, isBroadcasting]);

  // 표시할 데이터 결정 (로컬 연결 우선, 없으면 원격 데이터)
  const displayTelemetry = (isConnected && lastMessage?.type === 'telemetry')
    ? lastMessage.data
    : (isReplayMode ? telemetry : remoteData);

  // WebSocket 메시지 처리
  useEffect(() => {
    if (!lastMessage || isReplayMode) return;

    if (lastMessage.type === 'telemetry') {
      const data = lastMessage.data;
      console.log('[telemetry raw]', data);

      // 쿼터니안 Q15 디코딩
      const q1 = data.q1 / 32767.0;
      const q2 = data.q2 / 32767.0;
      const q3 = data.q3 / 32767.0;
      const q0 = Math.sqrt(Math.max(0, 1 - q1 ** 2 - q2 ** 2 - q3 ** 2));

      // flag1 파싱 (비트 순서 반대)
      const connectPin = Boolean(data.flag1 & 0x01);
      const parachute  = Boolean((data.flag1 >> 1) & 0x01);
      const ejectBtn   = Boolean((data.flag1 >> 2) & 0x01);
      const soundBtn   = Boolean((data.flag1 >> 3) & 0x01);
      const sats       = (data.flag1 >> 4) & 0x0F;

      // flag2 파싱 (비트 순서 반대)
      const flightPhase    = data.flag2 & 0x07;
      const ejectTimer     = Boolean((data.flag2 >> 3) & 0x01);
      const ejectDescent   = Boolean((data.flag2 >> 4) & 0x01);
      const extra1 = Boolean((data.flag2 >> 5) & 0x01);

      // 낙하산 사출 이유
      const parachuteEjectReason = ejectBtn ? 1 : ejectDescent ? 2 : ejectTimer ? 3 : 0;

      // roll 스케일링 (0~255 → -127~128°)
      const roll = data.roll - 127;

      // 이벤트 처리 (상태 변경 시에만)
      if (!telemetry.ejectBtn && ejectBtn && parachute) {
        playSound("/sounds/ssagal.mp3");
        toast.success('비상 사출이 감지되었습니다.');
      }
      if (!telemetry.soundBtn && soundBtn) {
        playSound("/sounds/count.mp3");
        toast.success("카운트다운이 시작되었습니다.");
      }

      setTelemetry({
        latitude: data.lat / 1e7,
        longitude: data.lon / 1e7,
        altitude: data.alt,
        q0, q1, q2, q3,
        roll,
        temperature: data.temp - 20,
        sats,
        soundBtn,
        ejectBtn,
        parachute,
        connectPin,
        flightPhase,
        extra1,
        ejectDescent,
        ejectTimer,
        stage: flightPhaseToStageMap[flightPhase] || 'pre-launch',
        parachuteEjectReason,
      });
   

      const newEntry = { t: Date.now(), roll };
      const updated = [...rollHistoryRef.current, newEntry].slice(-ROLL_HISTORY_MAX);
      rollHistoryRef.current = updated;
      setRollHistory(updated);
    } else if (lastMessage.type === 'recording_started') {
      console.log('기록 시작됨:', lastMessage.recordingId);
    } else if (lastMessage.type === 'recording_stopped') {
      console.log('기록 저장됨:', lastMessage.record);
      handleSaveToFirebase(lastMessage.record);
    } else if (lastMessage.type === 'command_success') {
      toast.success(lastMessage.message);
    } else if (lastMessage.type === 'error') {
      toast.error(lastMessage.message);
    }
  }, [lastMessage, isRecording, isReplayMode]);

  const handleSaveToFirebase = async (record: any) => {
    try {
      console.log("Firebase 저장 시도. 원래 데이터 크기(포인트):", record.telemetryData?.length);

      let finalRecord = { ...record };
      let serialized = JSON.stringify(finalRecord);
      let sizeInBytes = serialized.length;

      // Firestore 1MB 제한 대비 (안전을 위해 900KB 기준)
      const MAX_SIZE = 900 * 1024;

      if (sizeInBytes > MAX_SIZE) {
        console.warn(`데이터 크기(${(sizeInBytes / 1024).toFixed(1)}KB)가 제한을 초과하여 다운샘플링을 시도합니다.`);

        const ratio = Math.ceil(sizeInBytes / MAX_SIZE);
        finalRecord.telemetryData = record.telemetryData.filter((_: any, i: number) => i % ratio === 0);

        serialized = JSON.stringify(finalRecord);
        sizeInBytes = serialized.length;

        toast.warning(`데이터가 너무 커서 ${ratio}:1로 압축하여 저장합니다. (${(sizeInBytes / 1024).toFixed(1)}KB)`);
      }

      console.log(`최종 저장 데이터 크기: ${(sizeInBytes / 1024).toFixed(1)} KB`);

      const docRef = await addDoc(collection(db, "launches"), finalRecord);
      console.log("Firebase에 문서 저장됨 ID:", docRef.id);
      toast.success('발사 기록이 파이어베이스에 성공적으로 업로드되었습니다!');
    } catch (e: any) {
      console.error("Firebase 저장 에러:", e);
      toast.error(`파이어베이스 저장 실패: ${e.message || 'Firestore 용량 제한 또는 권한 문제입니다.'}`);
    }
  };

  // 리플레이 모드 초기화 및 재생 로직
  useEffect(() => {
    if (replayLaunch) {
      setIsReplayMode(true);
      setReplayData(replayLaunch);
      setReplayTime(0);
      setIsReplayPlaying(true);
      setIsRecording(false);
    }
  }, [replayLaunch]);

  useEffect(() => {
    if (!isReplayMode || !isReplayPlaying || !replayData) return;
    const interval = setInterval(() => {
      setReplayTime((prev) => {
        const next = prev + replaySpeed * 0.1;
        if (next >= replayData.duration) {
          setIsReplayPlaying(false);
          return replayData.duration;
        }
        return next;
      });
    }, 100);
    return () => clearInterval(interval);
  }, [isReplayMode, isReplayPlaying, replaySpeed, replayData]);

  useEffect(() => {
    if (!isReplayMode || !replayData) return;
    const dataIndex = Math.floor((replayTime / replayData.duration) * replayData.telemetryData.length);
    const currentData = replayData.telemetryData[dataIndex] || replayData.telemetryData[0];

    let convertedTelemetry: RocketTelemetry;

    // 원본 양식 데이터 처리
    if (currentData.lat !== undefined) {
      // 원본 센서 양식 (lat, lon, q1, q2, q3, flag1, flag2, roll, temp)
      convertedTelemetry = convertRawTelemetry(currentData);
    } else {
      // 이미 변환된 양식 (latitude, longitude, q0, ...)
      convertedTelemetry = {
        latitude: currentData.latitude,
        longitude: currentData.longitude,
        altitude: currentData.altitude,
        q0: currentData.q0 ?? 1,
        q1: currentData.q1 ?? 0,
        q2: currentData.q2 ?? 0,
        q3: currentData.q3 ?? 0,
        roll: currentData.roll ?? 0,
        temperature: currentData.temperature,
        sats: currentData.sats ?? 0,
        soundBtn: currentData.soundBtn ?? false,
        ejectBtn: currentData.ejectBtn ?? false,
        parachute: currentData.parachute ?? false,
        connectPin: currentData.connectPin ?? false,
        flightPhase: currentData.flightPhase ?? 0,
        extra1: currentData.extra1 ?? false,
        ejectDescent: currentData.ejectDescent ?? false,
        ejectTimer: currentData.ejectTimer ?? false,
        stage: flightPhaseToStageMap[currentData.flightPhase] || 'pre-launch',
        parachuteEjectReason: currentData.parachuteEjectReason ?? 0,
      };
    }

    setTelemetry(convertedTelemetry);

    // roll 히스토리 업데이트
    const newEntry = { t: Date.now(), roll: convertedTelemetry.roll };
    const updated = [...rollHistoryRef.current, newEntry].slice(-ROLL_HISTORY_MAX);
    rollHistoryRef.current = updated;
    setRollHistory(updated);
  }, [isReplayMode, replayTime, replayData]);

  const handleStartRecording = () => {
    setIsRecording(true);
    recordingStartTime.current = Date.now();
    const launchSiteString = `${telemetry.latitude.toFixed(6)}, ${telemetry.longitude.toFixed(6)}`;
    sendMessage({
      type: 'start_recording',
      data: { launchSite: launchSiteString },
    });
  };

  const handleStopRecording = () => {
    const launchName = prompt("발사 기록의 이름을 입력하세요:", `발사 ${new Date().toLocaleString('ko-KR')}`);
    if (launchName === null) return;

    setIsRecording(false);
    sendMessage({
      type: 'stop_recording',
      data: { name: launchName },
    });
  };

  const handleExitReplay = () => {
    setIsReplayMode(false);
    setReplayData(null);
    setReplayTime(0);
    setIsReplayPlaying(false);
    setTelemetry({
      latitude: 37.5665,
      longitude: 126.9780,
      altitude: 0,
      q0: 1, q1: 0, q2: 0, q3: 0,
      roll: 0,
      temperature: 0,
      sats: 0,
      soundBtn: false,
      ejectBtn: false,
      parachute: false,
      connectPin: false,
      flightPhase: 0,
      extra1: false,
      ejectDescent: false,
      ejectTimer: false,
      stage: 'pre-launch',
      parachuteEjectReason: 0,
    });
  };

  const handleToggleBroadcast = () => {
    if (!isConnected) {
      toast.error('로컬 서버와 연결된 상태에서만 중계를 시작할 수 있습니다.');
      return;
    }
    const nextState = !isBroadcasting;
    setIsBroadcasting(nextState);
    if (nextState) {
      toast.success('실시간 중계를 시작합니다.');
    } else {
      toast.info('실시간 중계를 중단했습니다.');
    }
  };

  const currentStatus = isConnected
    ? (isBroadcasting ? 'broadcasting' : 'local')
    : (remoteData ? 'remote' : 'disconnected');

  const statusDisplay = {
    broadcasting: { text: "실시간 중계 중", color: "text-red-500", icon: <Radio className="w-4 h-4 animate-pulse" /> },
    local: { text: "로컬 연결됨", color: "text-green-600", icon: <Signal className="w-4 h-4" /> },
    remote: { text: "원격 중계 수신 중", color: "text-[#ff385c]", icon: <Share2 className="w-4 h-4" /> },
    disconnected: { text: "연결 안됨", color: "text-[#929292]", icon: <Signal className="w-4 h-4 opacity-50" /> }
  }[currentStatus];

  const handleEmergencyEject = () => {sendMessage({ type: 'emergency_eject' }); playSound("/sounds/ssagal.mp3");}
  const handleCenterAlign = () => sendMessage({ type: 'center_align' });
  const handleReset = () => {sendMessage({ type: 'reset' });}

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (!isConnected) return;
      if (e.key === 'o') { e.preventDefault(); handleCenterAlign(); }
      else if (e.key === 'p') { e.preventDefault(); handleEmergencyEject(); }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isConnected]);


  const cardShadow = { boxShadow: 'rgba(0,0,0,0.02) 0 0 0 1px, rgba(0,0,0,0.04) 0 2px 6px 0, rgba(0,0,0,0.1) 0 4px 8px 0' };

  return (
    <div className="main-dashboard-content min-h-[calc(100vh-4rem)] p-2 sm:p-4 flex flex-col lg:h-[calc(100vh-4rem)]">
      <Toaster richColors position="top-center" />

      <style>{`
        @media (min-width: 1024px) {
          .main-dashboard-content {
            height: calc(100vh - 4rem) !important;
            overflow: hidden !important;
          }
          .dashboard-grid-container {
            display: grid !important;
            grid-template-columns: repeat(3, minmax(0, 1fr)) !important;
            gap: 1rem !important;
            flex: 1 !important;
            min-height: 0 !important;
            overflow: hidden !important;
          }
          .left-column-layout {
            grid-column: span 2 / span 2 !important;
            display: flex !important;
            flex-direction: column !important;
            gap: 1rem !important;
            min-height: 0 !important;
          }
          .right-column-layout {
            display: flex !important;
            flex-direction: column !important;
            gap: 1rem !important;
            overflow-y: auto !important;
            min-height: 0 !important;
          }
          .mapbox-view-container {
            flex: 1 !important;
            min-height: 0 !important;
            position: relative !important;
          }
          .rocket-orientation-container {
            flex: 1 !important;
            min-height: 0 !important;
          }
        }
      `}</style>

      <div className="dashboard-grid-container flex flex-col gap-4 flex-1 lg:overflow-hidden lg:h-full">
        {/* 왼쪽: 지도 및 기울기 */}
        <div className="left-column-layout flex flex-col gap-4 lg:h-full">
          {/* Mapbox 3D 지도 */}
          <div
            className="mapbox-view-container w-full bg-white rounded-xl overflow-hidden relative border border-[#dddddd] flex-shrink-0 lg:flex-1"
            style={{ height: '320px', ...cardShadow }}
          >
            <MapboxView telemetry={telemetry} />
          </div>

          {/* Three.js 로켓 기울기 */}
          <div
            className="rocket-orientation-container w-full h-64 bg-white rounded-xl overflow-hidden border border-[#dddddd]"
            style={cardShadow}
          >
            <RocketOrientation telemetry={telemetry} />
          </div>
        </div>

        {/* 오른쪽: 제어 패널 */}
        <div className="right-column-layout flex flex-col gap-4 h-full lg:overflow-y-auto hide-scrollbar">
          {/* 발사 단계 */}
          <div className="bg-white rounded-xl p-4 border border-[#dddddd]" style={cardShadow}>
            <LaunchStages stage={telemetry.stage} />
          </div>

          {/* 로켓 데이터 */}
          <div className="bg-white rounded-xl p-4 flex-1 border border-[#dddddd]" style={cardShadow}>
            <RocketData telemetry={telemetry} rollHistory={rollHistory} />
          </div>

          {/* 리플레이 컨트롤 */}
          {isReplayMode && replayData && (
            <div className="bg-white rounded-xl p-4 border border-[#ff385c]/30 space-y-4" style={cardShadow}>
              <ReplayControls
                currentTime={replayTime}
                duration={replayData.duration}
                isPlaying={isReplayPlaying}
                speed={replaySpeed}
                onTimeChange={setReplayTime}
                onPlayPause={() => setIsReplayPlaying(!isReplayPlaying)}
                onSpeedChange={setReplaySpeed}
                onReset={() => {
                  setReplayTime(0);
                  setIsReplayPlaying(false);
                }}
              />
              <button
                onClick={handleExitReplay}
                className="w-full bg-[#ff385c]/10 hover:bg-[#ff385c]/20 text-[#ff385c] border border-[#ff385c]/30 px-4 py-2.5 rounded-lg transition-all flex items-center justify-center gap-2 font-semibold text-sm"
              >
                <RotateCcw className="h-4 w-4" />
                실시간 모드로 전환
              </button>
            </div>
          )}

          {/* 제어 버튼 (실시간 모드 및 로컬 연결 시에만) */}
          {!isReplayMode && isConnected && (
            <div className="space-y-3 pb-4 lg:pb-0">
              {/* 사운드 및 비상 사출 */}
              <div className="bg-white rounded-xl p-4 border border-[#dddddd]" style={cardShadow}>
                <div className="flex gap-3">
                  <audio ref={audioRef} src="/sounds/silent.wav" />
                  <button
                    onClick={() => {
                      if (unlocked) {
                        playSound("/sounds/count.mp3");
                      } else {
                        unlockAudio();
                      }
                    }}
                    className="flex-1 bg-[#ff385c] hover:bg-[#e00b41] text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-semibold text-sm"
                  >
                    <Radio className="h-4 w-4" />
                    {unlocked ? "카운트다운" : "사운드 허용"}
                  </button>
                  <button
                    onClick={handleEmergencyEject}
                    className="flex-1 bg-red-600 hover:bg-red-500 text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-semibold text-sm shadow-lg shadow-red-900/30"
                  >
                    <Circle className="h-4 w-4" />
                    비상 사출
                  </button>
                </div>
              </div>

              {/* 기록 시작/중지 및 실시간 송신 */}
              <div className="bg-white rounded-xl p-4 border border-[#dddddd] space-y-3" style={cardShadow}>
                <button
                  onClick={handleReset}
                  className="w-full px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-semibold text-sm bg-red-600 hover:bg-red-500 text-white shadow-lg shadow-red-900/30"
                >
                  <Square className="h-4 w-4" />
                  보드 초기화
                </button>

                {!isRecording ? (
                  <button
                    onClick={handleStartRecording}
                    className="w-full bg-green-600 hover:bg-green-500 text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-semibold text-sm shadow-lg shadow-green-900/20"
                  >
                    <Square className="h-4 w-4" />
                    기록 시작
                  </button>
                ) : (
                  <button
                    onClick={handleStopRecording}
                    className="w-full bg-red-600 hover:bg-red-500 text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-semibold text-sm shadow-lg shadow-red-900/30 animate-pulse"
                  >
                    <Square className="h-4 w-4" />
                    기록 중지 및 저장
                  </button>
                )}

                <button
                  onClick={handleToggleBroadcast}
                  className={`w-full px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-semibold text-sm ${
                    isBroadcasting
                      ? 'bg-red-600 hover:bg-red-500 text-white shadow-lg shadow-red-900/30 animate-pulse'
                      : 'bg-white hover:bg-[#f7f7f7] text-[#222222] border border-[#dddddd]'
                  }`}
                >
                  <Share2 className="h-4 w-4" />
                  {isBroadcasting ? '실시간 송신 중지' : '실시간 송신'}
                </button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}