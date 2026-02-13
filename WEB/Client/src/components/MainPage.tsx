import { useState, useEffect, useRef } from 'react';
import { useLocation } from 'react-router-dom';
import MapboxView from './MapboxView';
import RocketOrientation from './RocketOrientation';
import LaunchStages from './LaunchStages';
import RocketData from './RocketData';
import ReplayControls from './ReplayControls';
import { Activity, RotateCcw, Circle, Square } from 'lucide-react';
import { useWebSocket } from '../hooks/useWebSocket';
import { Toaster, toast } from 'sonner';

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
  const audioRef = useRef(null);
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
    audioRef.current.play(); // 이제는 언제든지 재생 가능
  };

  useEffect(() => {
    let timer: NodeJS.Timeout;
    if (isConnected) {
      setShowConnectedBanner(true);
      timer = setTimeout(() => {
        setShowConnectedBanner(false);
      }, 3000);
    } else {
      setShowConnectedBanner(false); // 연결이 끊어지면 즉시 숨김
    }

    return () => {
      clearTimeout(timer);
    };
  }, [isConnected]);

  // WebSocket 메시지 처리
  useEffect(() => {
    if (!lastMessage || isReplayMode) return;

    if (lastMessage.type === 'telemetry') {
      // 실시간 텔레메트리 데이터 수신
      const data = lastMessage.data;

      if(data.parachuteStatus == 2)
      {
        toast.success(data.message ||'비상 사출 명령을 성공적으로 전송했습니다.');

      }
      if(data.connect == 3) { //커넥트핀 해제
        toast.success(data.message || "카운트다운 시작");
        playLater();
        data.connect = 1;
      }
      if(data.connect == 2) { //커넥트핀 연결
        toast.success(data.message ||"카운트다운 시작");
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
        // 서버에서 받은 flightPhase 값으로 stage를 결정
        stage: flightPhaseToStageMap[data.flightPhase] || 'pre-launch',
        temperature: data.temperature,
        pressure: data.pressure,
        battery: data.battery,
        connect: data.connect, // 커넥트핀
        parachuteStatus: data.parachuteStatus, // 낙하산 사출유무
        flightPhase: data.flightPhase, // 발사 단계
      });
    } else if (lastMessage.type === 'recording_started') {
      console.log('기록 시작됨:', lastMessage.recordingId);
    } else if (lastMessage.type === 'recording_stopped') {
      console.log('기록 저장됨:', lastMessage.record);
      alert('발사 기록이 성공적으로 저장되었습니다!');
    }
  }, [lastMessage, isRecording]);

  // 리플레이 모드 초기화
  useEffect(() => {
    if (replayLaunch) {
      setIsReplayMode(true);
      setReplayData(replayLaunch);
      setReplayTime(0);
      setIsReplayPlaying(true);
      setIsRecording(false);
    }
  }, [replayLaunch]);

  // 리플레이 재생
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

  // 리플레이 데이터로 텔레메트리 업데이트
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
      // 리플레이 데이터의 flightPhase를 사용하여 stage 결정
      stage: flightPhaseToStageMap[currentData.flightPhase] || 'pre-launch',
      temperature: currentData.temperature,
      pressure: currentData.pressure,
      battery: currentData.battery,
      connect: currentData.connect,
      parachuteStatus: currentData.parachuteStatus,
      flightPhase: currentData.flightPhase,
    });
  }, [isReplayMode, replayTime, replayData]);

  

  // 이전 determineStage 함수는 더 이상 사용되지 않습니다. (서버의 flightPhase를 사용)
  // const determineStage = (altitude: number): RocketTelemetry['stage'] => {
  //   if (altitude <= 0.1 && speed <= 0.1) return 'landed';
  //   if (altitude > 0.1 && speed > 0.1) {
  //     if (altitude <= 100) return 'launch';
  //     if (altitude <= 2000 && speed < 50) return 'parachute_deployment';
  //     if (altitude <= 5000) return 'ascent';
  //     if (altitude > 5000) return 'descent';
  //   }
  //   return 'pre-launch'; // Fallback
  // };

  const handleStartRecording = () => {
    setIsRecording(true);
    recordingStartTime.current = Date.now();
    
    // 현재 위치를 발사 장소로 설정
    const launchSiteString = `${telemetry.latitude.toFixed(6)}, ${telemetry.longitude.toFixed(6)}`;

    sendMessage({
      type: 'start_recording',
      data: {
        launchSite: launchSiteString,
      },
    });
  };

  const handleStopRecording = () => {
    // 1. 사용자에게 발사 기록의 이름을 입력받습니다.
    const launchName = prompt("발사 기록의 이름을 입력하세요:", `발사 ${new Date().toLocaleString('ko-KR')}`);

    // 2. 사용자가 입력을 취소한 경우 (null 반환) 아무 작업도 하지 않습니다.
    if (launchName === null) {
      return;
    }

    setIsRecording(false);
    sendMessage({
      type: 'stop_recording',
      data: {
        name: launchName, // 3. 입력받은 이름을 데이터에 담아 전송합니다.
      },
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

  const handleEmergencyEject = async () => {
    //if (window.confirm('정말로 비상 사출 명령을 보내시겠습니까?')) {
      try {
        const response = await fetch('/api/emergency-eject', { method: 'POST' });
        const data = await response.json();
        if (!response.ok) {
          throw new Error(data.message || '알 수 없는 오류가 발생했습니다.');
        }
        toast.success(data.message ||'비상 사출 명령을 성공적으로 전송했습니다.');
      } catch (error: any) {
        toast.error(`명령 전송 실패: ${error.message}`);
      }
    //}
  };

  const handleCenterAlign = async () => { //소리 출력
    //if (window.confirm('정말로 중앙 정렬 명령을 보내시겠습니까?')) {
      try {
        const response = await fetch('/api/center-align', { method: 'POST' });
        const data = await response.json();
        if (!response.ok) {
          throw new Error(data.message || '알 수 없는 오류가 발생했습니다.');
        }
        toast.success(data.message || '카운트다운 시작');
      } catch (error: any) {
        toast.error(`명령 전송 실패: ${error.message}`);
      }
    //}
  };

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // 입력창에서 타이핑 중일 때는 무시하고 싶으면 아래 주석 해제
      // const tag = (e.target as HTMLElement)?.tagName;
      // if (tag === 'INPUT' || tag === 'TEXTAREA') return;
  
      if (!isConnected) return; // 연결 안 되어 있으면 무시
  
      if (e.key === 'o') {
        e.preventDefault();
        handleCenterAlign();
      } else if (e.key === 'p') {
        e.preventDefault();
        handleEmergencyEject();
      }
    };
  
    window.addEventListener('keydown', handleKeyDown);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, [isConnected, handleCenterAlign, handleEmergencyEject]);

  
  

  return (
    <div className="h-[calc(100vh-4rem)] p-4 overflow-hidden flex flex-col">
      <Toaster richColors position="top-center" />
      {/* 연결 상태 표시 */}
      {!isReplayMode && !isConnected && (
        <div className="bg-red-600 text-white px-4 py-2 rounded-lg mb-4 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <Circle className="h-3 w-3" fill="currentColor" />
            <span>시리얼 서버 연결 안됨 (localhost:3001)</span>
          </div>
        </div>
      )}
      {!isReplayMode && showConnectedBanner && (
         <div className="bg-green-600 text-white px-4 py-2 rounded-lg mb-4 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <Circle className="h-3 w-3 animate-pulse" fill="currentColor" />
            <span>시리얼 서버 연결됨</span>
          </div>
        </div>
      )}

      {/* 리플레이 모드 배너 */}
      {isReplayMode && replayData && (
        <div className="bg-purple-600 text-white px-4 py-2 rounded-lg mb-4 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <Activity className="h-5 w-5 animate-pulse" />
            <span>리플레이 모드: 발사 #{replayData.id} - {new Date(replayData.date).toLocaleString('ko-KR')}</span>
          </div>
          <button
            onClick={handleExitReplay}
            className="bg-white/20 hover:bg-white/30 px-3 py-1 rounded transition-colors flex items-center gap-2"
          >
            <RotateCcw className="h-4 w-4" />
            실시간 모드로 전환
          </button>
        </div>
      )}

      {/* 기록 중 배너 */}
      {isRecording && !isReplayMode && (
        <div className="bg-red-600 text-white px-4 py-2 rounded-lg mb-4 flex items-center justify-between animate-pulse">
          <div className="flex items-center gap-3">
            <Circle className="h-3 w-3" fill="currentColor" />
            <span>기록 중... {Math.floor((Date.now() - recordingStartTime.current) / 1000)}초</span>
          </div>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-4 flex-1 overflow-hidden">
        {/* 왼쪽: 지도 및 기울기 */}
        <div className="lg:col-span-2 flex flex-col gap-4 h-full">
          {/* Mapbox 3D 지도 */}
          <div className="flex-1 bg-gray-900 rounded-lg overflow-hidden">
            <MapboxView telemetry={telemetry} />
          </div>

          {/* Three.js 로켓 기울기 */}
          <div className="h-80 bg-gray-900 rounded-lg overflow-hidden">
            <RocketOrientation telemetry={telemetry} />
          </div>
        </div>

        {/* 오른쪽: 제어 패널 */}
        <div className="flex flex-col gap-4 h-full overflow-y-auto hide-scrollbar">
          {/* 발사 단계 */}
          <div className="bg-gray-900 rounded-lg p-4">
            <LaunchStages stage={telemetry.stage} />
          </div>

          {/* 로켓 데이터 */}
          <div className="bg-gray-900 rounded-lg p-4 flex-1">
            <RocketData telemetry={telemetry} />
          </div>

          {/* 리플레이 컨트롤 */}
          {isReplayMode && replayData && (
            <div className="bg-gray-900 rounded-lg p-4">
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
            </div>
          )}

          { !(isReplayMode || isRecording) && (
          <div className="bg-gray-900 rounded-lg p-4">
            <div className="flex gap-3">
            <audio ref={audioRef} src="/sounds/count.mp3" />
            {!unlocked ? (
                <button
                onClick={unlockAudio}
                disabled={!isConnected}
                className="flex-1 bg-yellow-500 hover:bg-yellow-700 text-white px-4 py-3 rounded-lg transition-colors disabled:bg-gray-600 disabled:cursor-not-allowed flex items-center justify-center gap-2"
              >
                <Circle className="h-5 w-5" />
                사운드 허용
              </button>
              ) : (
                <button
                onClick={playLater}
                disabled={!isConnected}
                className="flex-1 bg-yellow-500 hover:bg-yellow-700 text-white px-4 py-3 rounded-lg transition-colors disabled:bg-gray-600 disabled:cursor-not-allowed flex items-center justify-center gap-2"
              >
                <Circle className="h-5 w-5" />
                음소거 해제
              </button>
              )}

              <button
                onClick={handleEmergencyEject}
                disabled={!isConnected}
                className="flex-1 bg-red-600 hover:bg-red-700 text-white px-4 py-3 rounded-lg transition-colors disabled:bg-gray-600 disabled:cursor-not-allowed flex items-center justify-center gap-2"
              >
                <Circle className="h-5 w-5" />
                비상 사출
              </button>
            </div>
          </div>
          )}


          {/* 제어 버튼 */}
          {!isReplayMode && (
            <div className="bg-gray-900 rounded-lg p-4">
              {!isRecording ? (
                <button
                  onClick={handleStartRecording}
                  disabled={!isConnected}
                  className="w-full bg-green-600 hover:bg-green-700 text-white px-4 py-3 rounded-lg transition-colors disabled:bg-gray-600 disabled:cursor-not-allowed flex items-center justify-center gap-2"
                >
                  <Circle className="h-5 w-5" />
                  기록 시작
                </button>
              ) : (
                <button
                  onClick={handleStopRecording}
                  className="w-full bg-red-600 hover:bg-red-700 text-white px-4 py-3 rounded-lg transition-colors flex items-center justify-center gap-2"
                >
                  <Square className="h-5 w-5" />
                  기록 중지 및 저장
                </button>
              )}
            </div>
          )}

          

          
        </div>
      </div>
    </div>
  );
}