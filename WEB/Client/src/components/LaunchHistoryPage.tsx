import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { toast } from "sonner";
import { Play, Calendar, Clock, TrendingUp, MapPin, RefreshCw, Trash2, Download } from 'lucide-react';
import { useWebSocket } from '../hooks/useWebSocket';
import { db } from '../lib/firebase';
import { collection, query, orderBy, getDocs, deleteDoc, updateDoc, doc } from 'firebase/firestore';

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

  // 기록 목록 불러오기 (Firebase)
  const fetchLaunches = async () => {
    setIsLoading(true);
    try {
      const q = query(collection(db, "launches"), orderBy("date", "desc"));
      const querySnapshot = await getDocs(q);
      const fetchedLaunches = querySnapshot.docs.map(doc => ({
        ...doc.data(),
        id: doc.id,
        date: doc.data().date ? new Date(doc.data().date) : new Date(),
      })) as LaunchRecord[];
      setLaunches(fetchedLaunches);
    } catch (e) {
      console.error("Firebase 데이터 로딩 에러:", e);
      toast.error("데이터를 불러오는데 실패했습니다.");
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    fetchLaunches();
  }, []);

  // WebSocket 메시지 처리 (메인 이벤트용으로 축소)
  useEffect(() => {
    if (!lastMessage) return;

    if (lastMessage.type === 'error') {
      toast.error(lastMessage.message);
    }
  }, [lastMessage]);

  const handleReplay = (launch: LaunchRecord) => {
    navigate('/', { state: { replayLaunch: launch } });
  };

  const handleRefresh = () => {
    fetchLaunches();
  };

  const handleDelete = async (launchId: string) => {
    if (window.confirm(`정말로 이 발사 기록을 삭제하시겠습니까? 이 작업은 되돌릴 수 없습니다.`)) {
      try {
        await deleteDoc(doc(db, "launches", launchId));
        toast.success("기록이 삭제되었습니다.");
        setLaunches((prev: LaunchRecord[]) => prev.filter(l => l.id !== launchId));
      } catch (e) {
        console.error("삭제 실패:", e);
        toast.error("기록 삭제에 실패했습니다.");
      }
    }
  };

  const handleStatusToggle = async (launch: LaunchRecord) => {
    const currentIndex = statusCycle.indexOf(launch.status);
    const nextIndex = (currentIndex + 1) % statusCycle.length;
    const nextStatus = statusCycle[nextIndex];

    try {
      await updateDoc(doc(db, "launches", launch.id), { status: nextStatus });
      toast.success(`상태가 ${getStatusText(nextStatus)}로 변경되었습니다.`);
      setLaunches((prev: LaunchRecord[]) => prev.map(l => l.id === launch.id ? { ...l, status: nextStatus } : l));
    } catch (e) {
      console.error("상태 업데이트 실패:", e);
      toast.error("상태 업데이트에 실패했습니다.");
    }
  };

  const handleDownload = (launch: LaunchRecord) => {
    const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(launch, null, 2));
    const downloadAnchorNode = document.createElement('a');
    downloadAnchorNode.setAttribute("href", dataStr);
    downloadAnchorNode.setAttribute("download", `launch_${launch.id}.json`);
    document.body.appendChild(downloadAnchorNode);
    downloadAnchorNode.click();
    downloadAnchorNode.remove();
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
    <div className="min-h-screen bg-black text-white p-2 sm:p-4 overflow-y-auto">
      <div className="bg-gray-900 rounded-lg p-4 sm:p-6 w-full max-w-6xl mx-auto">
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
        {/* {!isConnected && (
          <div className="bg-red-600/20 border border-red-600 text-red-400 px-4 py-3 rounded-lg mb-4">
            시리얼 서버에 연결되지 않았습니다. localhost:3001에서 서버를 실행하세요.
          </div>
        )} */}

        {/* 통계 카드 - 모바일에서는 2컬럼 강제 */}
        <div className="grid grid-cols-2 lg:grid-cols-4 gap-3 mb-6">
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
        <div className="space-y-4">
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
                  <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 mb-4">
                    <div className="flex items-center gap-3">
                      <div className={`w-3 h-3 rounded-full flex-shrink-0 ${getStatusColor(launch.status)}`} />
                      <div>
                        <div className="text-white font-bold">{launch.name || `발사 #${launch.id.slice(0, 8)}`}</div>
                        <div className="text-xs text-gray-400 flex flex-wrap items-center gap-x-3 gap-y-1 mt-1">
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
                    <div className="flex items-center gap-2 w-full sm:w-auto overflow-x-auto pb-2 sm:pb-0 hide-scrollbar">
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleDownload(launch);
                        }}
                        className="bg-gray-700 hover:bg-gray-600 text-white p-2.5 rounded-lg text-sm flex items-center gap-1 transition-colors border border-white/5"
                        title="데이터 다운로드"
                      >
                        <Download className="w-4 h-4" />
                      </button>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleDelete(launch.id);
                        }}
                        className="bg-red-600/20 hover:bg-red-600/40 text-red-400 p-2.5 rounded-lg text-sm flex items-center gap-1 transition-colors border border-red-500/30"
                        title="기록 삭제"
                      >
                        <Trash2 className="w-4 h-4" />
                      </button>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleStatusToggle(launch);
                        }}
                        className={`text-xs px-3 py-2.5 rounded-lg text-white font-bold cursor-pointer transition-all ${getStatusColor(launch.status)}`}
                        title="상태 변경"
                      >
                        {getStatusText(launch.status)}
                      </button>
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          handleReplay(launch);
                        }}
                        className="bg-blue-600 hover:bg-blue-700 text-white px-4 py-2.5 rounded-lg text-sm flex items-center gap-2 transition-all shadow-lg shadow-blue-900/20 whitespace-nowrap"
                      >
                        <Play className="w-4 h-4 fill-current" />
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