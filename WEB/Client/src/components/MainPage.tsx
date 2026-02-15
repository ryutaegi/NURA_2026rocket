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
  latitude: number;
  longitude: number;
  altitude: number;
  speed: number;
  pitch: number;
  roll: number;
  yaw: number;
  // stage는 이제 flightPhase 값에 따라 결정
  stage: 'pre-launch' | 'launch' | 'powered' | 'coasting' | 'apogee' | 'descent' | 'landed';
  temperature: number;
  pressure: number;
  battery: number;
  connect: number; // 새로 추가
  parachuteStatus: number; // 새로 추가 (0: 닫힘, 1: 열림)
  flightPhase: number; // 새로 추가 (0: STANDBY, 1: LAUNCHED, ...)
}

// 아두이노 FlightState enum에 따른 매핑
export const flightPhaseToStageMap: { [key: number]: RocketTelemetry['stage'] } = {
  0: 'pre-launch',    // STANDBY
  1: 'launch',        // LAUNCHED
  2: 'powered',       // POWERED
  3: 'coasting',      // COASTING
  4: 'apogee',        // APOGEE
  5: 'descent',       // DESCENT
  6: 'landed',        // LANDED
};

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
    speed: 0,
    pitch: 0,
    roll: 0,
    yaw: 0,
    stage: 'pre-launch', // 초기값
    temperature: 0,
    pressure: 0,
    battery: 0,
    connect: 0, // 초기값
    parachuteStatus: 0, // 초기값
    flightPhase: 0, // 초기값
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


  const playLater = () => {
    if (audioRef.current) {
      audioRef.current.play(); // 이제는 언제든지 재생 가능
    }
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

      if (data.parachuteStatus == 2) {
        toast.success(data.message || '비상 사출 명령을 성공적으로 전송했습니다.');
      }

      if (data.connect == 3) { //커넥트핀 해제
        toast.success(data.message || "카운트다운이 시작되었습니다.");
        playLater();
        data.connect = 1;
      }

      if (data.connect == 2) { //커넥트핀 연결
        toast.success(data.message || "카운트다운이 시작되었습니다.");
        playLater();
        data.connect = 0;
      }

      setTelemetry({
        latitude: data.latitude,
        longitude: data.longitude,
        altitude: data.altitude,
        speed: data.speed,
        pitch: data.pitch,
        roll: data.roll,
        yaw: data.yaw,
        stage: flightPhaseToStageMap[data.flightPhase] || 'pre-launch',
        temperature: data.temperature,
        pressure: data.pressure,
        battery: data.battery,
        connect: data.connect,
        parachuteStatus: data.parachuteStatus,
        flightPhase: data.flightPhase,
      });
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
    setTelemetry({
      latitude: currentData.latitude,
      longitude: currentData.longitude,
      altitude: currentData.altitude,
      speed: currentData.speed,
      pitch: currentData.pitch,
      roll: currentData.roll,
      yaw: currentData.yaw,
      stage: flightPhaseToStageMap[currentData.flightPhase] || 'pre-launch',
      temperature: currentData.temperature,
      pressure: currentData.pressure,
      battery: currentData.battery,
      connect: currentData.connect,
      parachuteStatus: currentData.parachuteStatus,
      flightPhase: currentData.flightPhase,
    });
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
      speed: 0,
      pitch: 0,
      roll: 0,
      yaw: 0,
      stage: 'pre-launch',
      temperature: 22,
      pressure: 1013,
      battery: 100,
      connect: 0,
      parachuteStatus: 0,
      flightPhase: 0,
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
    local: { text: "로컬 연결됨", color: "text-green-500", icon: <Signal className="w-4 h-4" /> },
    remote: { text: "원격 중계 수신 중", color: "text-blue-500", icon: <Share2 className="w-4 h-4" /> },
    disconnected: { text: "연결 안됨", color: "text-gray-500", icon: <Signal className="w-4 h-4 opacity-50" /> }
  }[currentStatus];

  const handleEmergencyEject = () => sendMessage({ type: 'emergency_eject' });
  const handleCenterAlign = () => sendMessage({ type: 'center_align' });

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (!isConnected) return;
      if (e.key === 'o') { e.preventDefault(); handleCenterAlign(); }
      else if (e.key === 'p') { e.preventDefault(); handleEmergencyEject(); }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isConnected]);


  return (
    <div className="main-dashboard-content min-h-[calc(100vh-4rem)] p-2 sm:p-4 flex flex-col lg:h-[calc(100vh-4rem)]">
      <Toaster richColors position="top-center" />

      {/* 대시보드 레이아웃 강제 수정을 위한 스타일 */}
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

      {/* 배너 영역 */}
      <div className="flex flex-col gap-2 mb-4">
        {/* ... existing banner logic ... */}
      </div>

      <div className="dashboard-grid-container flex flex-col gap-4 flex-1 lg:overflow-hidden lg:h-full">
        {/* 왼쪽: 지도 및 기울기 */}
        <div className="left-column-layout flex flex-col gap-4 lg:h-full">
          {/* Mapbox 3D 지도 - 모바일에서 확실한 높이 보장 */}
          <div
            className="mapbox-view-container w-full bg-gray-950 rounded-xl overflow-hidden relative border border-white/5 flex-shrink-0 lg:flex-1"
            style={{ height: '320px' }}
          >
            <MapboxView telemetry={telemetry} />
          </div>

          {/* Three.js 로켓 기울기 */}
          <div className="rocket-orientation-container w-full h-64 bg-gray-900 rounded-xl overflow-hidden border border-white/5">
            <RocketOrientation telemetry={telemetry} />
          </div>
        </div>

        {/* 오른쪽: 제어 패널 */}
        <div className="right-column-layout flex flex-col gap-4 h-full lg:overflow-y-auto hide-scrollbar">
          {/* 발사 단계 */}
          <div className="bg-gray-900/50 backdrop-blur-sm rounded-xl p-4 border border-white/5">
            <LaunchStages stage={telemetry.stage} />
          </div>

          {/* 로켓 데이터 */}
          <div className="bg-gray-900/50 backdrop-blur-sm rounded-xl p-4 flex-1 border border-white/5">
            <RocketData telemetry={telemetry} />
          </div>

          {/* 리플레이 컨트롤 */}
          {isReplayMode && replayData && (
            <div className="bg-gray-900/80 backdrop-blur-sm rounded-xl p-4 border border-blue-500/30 space-y-4">
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
                className="w-full bg-blue-600/20 hover:bg-blue-600/40 text-blue-400 border border-blue-500/30 px-4 py-2.5 rounded-lg transition-all flex items-center justify-center gap-2 font-bold text-sm"
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
              <div className="bg-gray-900/50 backdrop-blur-sm rounded-xl p-4 border border-white/5">
                <div className="flex gap-3">
                  <audio ref={audioRef} src="/sounds/count.mp3" />
                  <button
                    onClick={unlocked ? playLater : unlockAudio}
                    className={`flex-1 ${unlocked ? 'bg-blue-600 hover:bg-blue-500' : 'bg-yellow-600 hover:bg-yellow-500'} text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-black text-sm shadow-lg`}
                  >
                    <Radio className="h-4 w-4" />
                    {unlocked ? "카운트다운" : "사운드 허용"}
                  </button>

                  <button
                    onClick={handleEmergencyEject}
                    className="flex-1 bg-red-600 hover:bg-red-500 text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-black text-sm shadow-lg shadow-red-900/40"
                  >
                    <Circle className="h-4 w-4" />
                    비상 사출
                  </button>
                </div>
              </div>

              {/* 기록 시작/중지 및 실시간 송신 */}
              <div className="bg-gray-900/50 backdrop-blur-sm rounded-xl p-4 border border-white/5 space-y-3">
                {!isRecording ? (
                  <button
                    onClick={handleStartRecording}
                    className="w-full bg-green-600 hover:bg-green-500 text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-black text-sm shadow-lg shadow-green-900/40"
                  >
                    <Square className="h-4 w-4" />
                    기록 시작
                  </button>
                ) : (
                  <button
                    onClick={handleStopRecording}
                    className="w-full bg-red-600 hover:bg-red-500 text-white px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-black text-sm shadow-lg shadow-red-900/40 animate-pulse"
                  >
                    <Square className="h-4 w-4" />
                    기록 중지 및 저장
                  </button>
                )}

                {/* 실시간 송신 버튼 */}
                <button
                  onClick={handleToggleBroadcast}
                  className={`w-full px-4 py-3 rounded-lg transition-all duration-200 transform hover:scale-[1.02] active:scale-[0.98] flex items-center justify-center gap-2 font-black text-sm shadow-lg ${isBroadcasting
                    ? 'bg-red-600 hover:bg-red-500 text-white shadow-red-900/40 animate-pulse'
                    : 'bg-gray-800 hover:bg-gray-700 text-gray-300 border border-white/5'
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