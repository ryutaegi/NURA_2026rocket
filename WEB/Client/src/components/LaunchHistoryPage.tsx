import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { toast } from "sonner";
import { Play, Calendar, Clock, TrendingUp, MapPin, RefreshCw, Trash2, Download } from 'lucide-react';
import { useWebSocket } from '../hooks/useWebSocket';

interface LaunchRecord {
  id: string;
  name: string; // "name" 필드 추가
  date: Date;
  maxAltitude: number;
  maxSpeed: number;
  duration: number;
  status: 'success' | 'partial' | 'failed';
  launchSite: string;
  landingCoords: { lat: number; lng: number };
  telemetryData: any[];
}

export default function LaunchHistoryPage() {
  const navigate = useNavigate();
  const { isConnected, lastMessage, sendMessage } = useWebSocket();
  const [launches, setLaunches] = useState<LaunchRecord[]>([]);
  const [isLoading, setIsLoading] = useState(true);

  // 상태 순환 로직 정의
  const statusCycle: ('success' | 'partial' | 'failed')[] = ['success', 'partial', 'failed'];

  // 기록 목록 불러오기
  useEffect(() => {
    if (isConnected) {
      sendMessage({ type: 'get_recordings' });
    }
  }, [isConnected]);

  // WebSocket 메시지 처리
  useEffect(() => {
    if (!lastMessage) return;

    if (lastMessage.type === 'recordings_list') {
      const records = lastMessage.records || [];
      const validRecords = records
        .filter(record => 
          record && 
          record.date && 
          typeof record.maxAltitude === 'number' &&
          typeof record.maxSpeed === 'number'
        ) // 유효한 레코드만 필터링
        .map((record: any) => ({
          ...record,
          date: new Date(record.date),
          telemetryData: [],
        }));
      setLaunches(validRecords);
      setIsLoading(false);
    } else if (lastMessage.type === 'recording_data') {
      // 리플레이 데이터 수신
      const record = lastMessage.record;
      handleReplay(record);
    } else if (lastMessage.type === 'recording_deleted') {
      // 기록 삭제 확인
      toast.success(`발사 기록 #${lastMessage.recordingId.slice(0, 8)}...이(가) 삭제되었습니다.`);
      setLaunches(prevLaunches => prevLaunches.filter(l => l.id !== lastMessage.recordingId));
    } else if (lastMessage.type === 'launch_status_updated') {
      // 상태 업데이트 확인
      toast.success(`발사 기록 #${lastMessage.recordingId.slice(0, 8)}의 상태가 ${getStatusText(lastMessage.newStatus)}로 업데이트되었습니다.`);
      setLaunches(prevLaunches => prevLaunches.map(l => 
        l.id === lastMessage.recordingId ? { ...l, status: lastMessage.newStatus } : l
      ));
    } else if (lastMessage.type === 'error') {
      toast.error(lastMessage.message);
    }
  }, [lastMessage]);

  const handleReplay = (launch: LaunchRecord | { id: string }) => {
    // 전체 데이터 요청
    if (!('telemetryData' in launch) || launch.telemetryData.length === 0) {
      sendMessage({ 
        type: 'get_recording_data',
        recordingId: launch.id,
      });
    } else {
      navigate('/', { state: { replayLaunch: launch } });
    }
  };

  const handleRefresh = () => {
    setIsLoading(true);
    sendMessage({ type: 'get_recordings' });
  };

  const handleDelete = (launchId: string) => {
    if (window.confirm(`정말로 이 발사 기록을 삭제하시겠습니까? 이 작업은 되돌릴 수 없습니다.`)) {
      sendMessage({ type: 'delete_recording', recordingId: launchId });
    }
  };

  const handleStatusToggle = (launch: LaunchRecord) => {
    const currentIndex = statusCycle.indexOf(launch.status);
    const nextIndex = (currentIndex + 1) % statusCycle.length;
    const nextStatus = statusCycle[nextIndex];
    sendMessage({ type: 'update_launch_status', recordingId: launch.id, newStatus: nextStatus });
  };

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'success':
        return 'bg-green-600';
      case 'partial':
        return 'bg-yellow-600';
      case 'failed':
        return 'bg-red-600';
      default:
        return 'bg-gray-600';
    }
  };

  const getStatusText = (status: string) => {
    switch (status) {
      case 'success':
        return '성공';
      case 'partial':
        return '부분 성공';
      case 'failed':
        return '실패';
      default:
        return '알 수 없음';
    }
  };

  return (
    <div className="h-[calc(100vh-4rem)] p-4 overflow-hidden flex flex-col">
      <div className="bg-gray-900 rounded-lg p-6 flex flex-col flex-1 overflow-y-auto">
        <div className="mb-6 flex items-center justify-between">
          <div>
            <h1 className="text-2xl text-white mb-2">발사 기록</h1>
            <p className="text-gray-400 text-sm">과거 발사 기록을 확인하고 리플레이할 수 있습니다</p>
          </div>
          <button
            onClick={handleRefresh}
            disabled={!isConnected}
            className="bg-blue-600 hover:bg-blue-700 disabled:bg-gray-600 text-white px-4 py-2 rounded-lg transition-colors flex items-center gap-2"
          >
            <RefreshCw className={`w-4 h-4 ${isLoading ? 'animate-spin' : ''}`} />
            새로고침
          </button>
        </div>

        {/* 연결 상태 */}
        {!isConnected && (
          <div className="bg-red-600/20 border border-red-600 text-red-400 px-4 py-3 rounded-lg mb-4">
            시리얼 서버에 연결되지 않았습니다. localhost:3001에서 서버를 실행하세요.
          </div>
        )}

        {/* 통계 카드 */}
        <div className="grid grid-cols-1 md:grid-cols-4 gap-4 mb-6">
          <div className="bg-gray-800 rounded-lg p-4">
            <div className="text-gray-400 text-sm mb-1">총 발사 횟수</div>
            <div className="text-2xl text-white">{launches.length}</div>
          </div>
          <div className="bg-gray-800 rounded-lg p-4">
            <div className="text-gray-400 text-sm mb-1">성공률</div>
            <div className="text-2xl text-green-400">
              {launches.length > 0 ? ((launches.filter(l => l.status === 'success').length / launches.length) * 100).toFixed(0) : 0}%
            </div>
          </div>
          <div className="bg-gray-800 rounded-lg p-4">
            <div className="text-gray-400 text-sm mb-1">최고 고도</div>
            <div className="text-2xl text-blue-400">
              {launches.length > 0 ? Math.max(...launches.map(l => l.maxAltitude)).toLocaleString() : 0}m
            </div>
          </div>
          <div className="bg-gray-800 rounded-lg p-4">
            <div className="text-gray-400 text-sm mb-1">최고 속도</div>
            <div className="text-2xl text-purple-400">
              {launches.length > 0 ? Math.max(...launches.map(l => l.maxSpeed)) : 0} m/s
            </div>
          </div>
        </div>

        {/* 발사 기록 리스트 */}
        <div className="flex-1 overflow-y-auto hide-scrollbar">
          {isLoading ? (
            <div className="flex items-center justify-center h-full">
              <div className="text-gray-400">로딩 중...</div>
            </div>
          ) : launches.length === 0 ? (
            <div className="flex items-center justify-center h-full">
              <div className="text-center text-gray-400">
                <p className="mb-2">저장된 발사 기록이 없습니다.</p>
                <p className="text-sm">메인 페이지에서 기록을 시작하세요.</p>
              </div>
            </div>
          ) : (
            <div className="space-y-3">
              {launches.map((launch) => (
                <div
                  key={launch.id}
                  className="bg-gray-800 rounded-lg p-4 hover:bg-gray-750 transition-all"
                >
                  <div className="flex items-start justify-between mb-3">
                    <div className="flex items-center gap-3">
                      <div className={`w-3 h-3 rounded-full ${getStatusColor(launch.status)}`} />
                      <div>
                        <div className="text-white">{launch.name || `발사 #${launch.id.slice(0, 8)}`}</div>
                        <div className="text-xs text-gray-400 flex items-center gap-3 mt-1">
                          <span className="flex items-center gap-1">
                            <Calendar className="w-3 h-3" />
                            {launch.date && !isNaN(launch.date.getTime())
                              ? launch.date.toLocaleDateString('ko-KR')
                              : '날짜 정보 없음'}
                          </span>
                          <span className="flex items-center gap-1">
                            <Clock className="w-3 h-3" />
                            {launch.date && !isNaN(launch.date.getTime())
                              ? launch.date.toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit' })
                              : '시간 정보 없음'}
                          </span>
                        </div>
                      </div>
                    </div>
                    <div className="flex items-center gap-2">
                      <a
                        href={`http://localhost:3001/launch-data/launch_${launch.id}.json`}
                        download={`launch_${launch.id}.json`}
                        onClick={(e) => e.stopPropagation()}
                        className="bg-gray-600 hover:bg-gray-700 text-white p-2 rounded text-sm flex items-center gap-1 transition-colors"
                        title="데이터 다운로드"
                      >
                        <Download className="w-4 h-4" />
                      </a>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleDelete(launch.id);
                        }}
                        className="bg-red-600 hover:bg-red-700 text-white p-2 rounded text-sm flex items-center gap-1 transition-colors"
                        title="기록 삭제"
                      >
                        <Trash2 className="w-4 h-4" />
                      </button>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleStatusToggle(launch);
                        }}
                        className={`text-xs px-2 py-2 rounded text-white cursor-pointer ${getStatusColor(launch.status)}`}
                        title="상태 변경"
                      >
                        {getStatusText(launch.status)}
                      </button>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleReplay(launch);
                        }}
                        className="bg-blue-600 hover:bg-blue-700 text-white px-3 py-2 rounded text-sm flex items-center gap-2 transition-colors"
                      >
                        <Play className="w-4 h-4" />
                        리플레이
                      </button>
                    </div>
                  </div>

                  <div className="grid grid-cols-2 md:grid-cols-4 gap-4 text-sm">
                    <div>
                      <div className="text-gray-400 text-xs mb-1">최대 고도</div>
                      <div className="text-blue-400 flex items-center gap-1">
                        <TrendingUp className="w-3 h-3" />
                        {typeof launch.maxAltitude === 'number' ? launch.maxAltitude.toLocaleString() : 'N/A'} m
                      </div>
                    </div>
                    <div>
                      <div className="text-gray-400 text-xs mb-1">최대 속도</div>
                      <div className="text-purple-400">{launch.maxSpeed.toFixed(1)} m/s</div>
                    </div>
                    <div>
                      <div className="text-gray-400 text-xs mb-1">녹화 시간</div>
                      <div className="text-green-400">{launch.duration.toFixed(0)}초</div>
                    </div>
                    <div>
                      <div className="text-gray-400 text-xs mb-1 flex items-center gap-1">
                        <MapPin className="w-3 h-3" />
                        발사 장소
                      </div>
                      <div className="text-white">{launch.launchSite}</div>
                    </div>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}