import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { toast } from "sonner";
import { Play, Calendar, Clock, TrendingUp, MapPin, RefreshCw, Trash2, Download } from 'lucide-react';
import { useWebSocket } from '../hooks/useWebSocket';
import { db } from '../lib/firebase';
import { collection, query, orderBy, getDocs, deleteDoc, updateDoc, doc } from 'firebase/firestore';

interface LaunchRecord {
  id: string;
  name: string;
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
  const { isConnected, lastMessage } = useWebSocket();
  const [launches, setLaunches] = useState<LaunchRecord[]>([]);
  const [isLoading, setIsLoading] = useState(true);

  const statusCycle: ('success' | 'partial' | 'failed')[] = ['success', 'partial', 'failed'];

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

  useEffect(() => {
    if (!lastMessage) return;
    if (lastMessage.type === 'error') {
      toast.error(lastMessage.message);
    }
  }, [lastMessage]);

  const handleReplay = (launch: LaunchRecord) => {
    navigate('/', { state: { replayLaunch: launch } });
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

  const getStatusBadgeClass = (status: string) => {
    switch (status) {
      case 'success': return 'bg-green-100 text-green-700 border border-green-200';
      case 'partial': return 'bg-yellow-100 text-yellow-700 border border-yellow-200';
      case 'failed': return 'bg-red-100 text-red-600 border border-red-200';
      default: return 'bg-[#f2f2f2] text-[#6a6a6a] border border-[#dddddd]';
    }
  };

  const getStatusDot = (status: string) => {
    switch (status) {
      case 'success': return 'bg-green-500';
      case 'partial': return 'bg-yellow-500';
      case 'failed': return 'bg-red-500';
      default: return 'bg-[#dddddd]';
    }
  };

  const getStatusText = (status: string) => {
    switch (status) {
      case 'success': return '성공';
      case 'partial': return '부분 성공';
      case 'failed': return '실패';
      default: return '알 수 없음';
    }
  };

  const cardShadow = { boxShadow: 'rgba(0,0,0,0.02) 0 0 0 1px, rgba(0,0,0,0.04) 0 2px 6px 0, rgba(0,0,0,0.1) 0 4px 8px 0' };

  return (
    <div className="min-h-screen bg-[#f7f7f7] p-2 sm:p-6 overflow-y-auto">
      <div className="bg-white rounded-xl w-full max-w-4xl mx-auto" style={cardShadow}>
        {/* 헤더 */}
        <div className="p-5 sm:p-6 border-b border-[#ebebeb]">
          <div className="flex items-center justify-between gap-4">
            <div>
              <h1 className="text-[22px] font-semibold text-[#222222]">발사 기록</h1>
              <p className="text-[#6a6a6a] text-sm mt-0.5">과거 발사 기록을 확인하고 리플레이할 수 있습니다</p>
            </div>
            <button
              onClick={fetchLaunches}
              className="flex-shrink-0 bg-[#ff385c] hover:bg-[#e00b41] disabled:bg-[#ffd1da] text-white px-4 py-2.5 rounded-lg transition-colors flex items-center gap-2 text-sm font-medium"
            >
              <RefreshCw className={`w-4 h-4 ${isLoading ? 'animate-spin' : ''}`} />
              새로고침
            </button>
          </div>
        </div>

        <div className="p-5 sm:p-6">
          {/* 통계 카드 */}
          <div className="grid grid-cols-3 gap-3 mb-6">
            <div className="bg-[#f7f7f7] rounded-xl p-4 border border-[#dddddd]">
              <div className="text-[#6a6a6a] text-xs font-medium mb-1.5">총 발사 횟수</div>
              <div className="text-2xl font-semibold text-[#222222]">{launches.length}</div>
            </div>
            <div className="bg-[#f7f7f7] rounded-xl p-4 border border-[#dddddd]">
              <div className="text-[#6a6a6a] text-xs font-medium mb-1.5">성공률</div>
              <div className="text-2xl font-semibold text-green-600">
                {launches.length > 0
                  ? ((launches.filter(l => l.status === 'success').length / launches.length) * 100).toFixed(0)
                  : 0}%
              </div>
            </div>
            <div className="bg-[#f7f7f7] rounded-xl p-4 border border-[#dddddd]">
              <div className="text-[#6a6a6a] text-xs font-medium mb-1.5">최고 고도</div>
              <div className="text-2xl font-semibold text-[#ff385c]">
                {launches.length > 0 ? Math.max(...launches.map(l => l.maxAltitude)).toLocaleString() : 0}m
              </div>
            </div>
          </div>

          {/* 발사 기록 리스트 */}
          <div className="space-y-3">
            {isLoading ? (
              <div className="flex items-center justify-center py-16">
                <div className="text-[#6a6a6a] text-sm">로딩 중...</div>
              </div>
            ) : launches.length === 0 ? (
              <div className="flex items-center justify-center py-16">
                <div className="text-center">
                  <p className="text-[#6a6a6a] text-sm mb-1">저장된 발사 기록이 없습니다.</p>
                  <p className="text-xs text-[#929292]">메인 페이지에서 기록을 시작하세요.</p>
                </div>
              </div>
            ) : (
              launches.map((launch) => (
                <div
                  key={launch.id}
                  className="bg-white rounded-xl p-4 border border-[#dddddd] transition-all cursor-default"
                  onMouseEnter={e => {
                    e.currentTarget.style.boxShadow = 'rgba(0,0,0,0.02) 0 0 0 1px, rgba(0,0,0,0.04) 0 2px 6px 0, rgba(0,0,0,0.1) 0 4px 8px 0';
                    e.currentTarget.style.borderColor = '#c1c1c1';
                  }}
                  onMouseLeave={e => {
                    e.currentTarget.style.boxShadow = 'none';
                    e.currentTarget.style.borderColor = '#dddddd';
                  }}
                >
                  <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 mb-3">
                    <div className="flex items-center gap-3">
                      <div className={`w-2.5 h-2.5 rounded-full flex-shrink-0 ${getStatusDot(launch.status)}`} />
                      <div>
                        <div className="text-[#222222] font-semibold text-sm">
                          {launch.name || `발사 #${launch.id.slice(0, 8)}`}
                        </div>
                        <div className="text-xs text-[#6a6a6a] flex flex-wrap items-center gap-x-3 gap-y-1 mt-0.5">
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

                    <div className="flex items-center gap-2 w-full sm:w-auto overflow-x-auto pb-1 sm:pb-0 hide-scrollbar">
                      <button
                        onClick={(e) => { e.stopPropagation(); handleDownload(launch); }}
                        className="bg-[#f2f2f2] hover:bg-[#ebebeb] text-[#222222] p-2.5 rounded-lg flex items-center transition-colors border border-[#dddddd]"
                        title="데이터 다운로드"
                      >
                        <Download className="w-4 h-4" />
                      </button>
                      {isConnected && (
                        <button
                          onClick={(e) => { e.stopPropagation(); handleDelete(launch.id); }}
                          className="bg-red-50 hover:bg-red-100 text-red-500 p-2.5 rounded-lg flex items-center transition-colors border border-red-100"
                          title="기록 삭제"
                        >
                          <Trash2 className="w-4 h-4" />
                        </button>
                      )}
                      <button
                        onClick={(e) => {
                          e.stopPropagation();
                          if (isConnected) handleStatusToggle(launch);
                        }}
                        disabled={!isConnected}
                        className={`text-xs px-3 py-2 rounded-lg font-medium transition-all whitespace-nowrap ${
                          isConnected
                            ? `cursor-pointer ${getStatusBadgeClass(launch.status)}`
                            : 'cursor-default bg-[#f2f2f2] text-[#929292] border border-[#dddddd]'
                        }`}
                        title={isConnected ? '클릭하여 상태 변경' : '로컬 서버 연결 시 수정 가능'}
                      >
                        {getStatusText(launch.status)}
                      </button>
                      <button
                        onClick={(e) => { e.stopPropagation(); handleReplay(launch); }}
                        className="bg-[#ff385c] hover:bg-[#e00b41] text-white px-4 py-2.5 rounded-lg text-sm flex items-center gap-2 transition-all font-medium whitespace-nowrap"
                      >
                        <Play className="w-4 h-4 fill-current" />
                        리플레이
                      </button>
                    </div>
                  </div>

                  <div className="grid grid-cols-2 md:grid-cols-4 gap-3 pt-3 border-t border-[#ebebeb]">
                    <div>
                      <div className="text-[#929292] text-xs mb-1">최대 고도</div>
                      <div className="text-[#ff385c] flex items-center gap-1 text-sm font-medium">
                        <TrendingUp className="w-3 h-3" />
                        {typeof launch.maxAltitude === 'number' ? launch.maxAltitude.toLocaleString() : 'N/A'} m
                      </div>
                    </div>
                    <div>
                      <div className="text-[#929292] text-xs mb-1">녹화 시간</div>
                      <div className="text-green-600 text-sm font-medium">{launch.duration.toFixed(0)}초</div>
                    </div>
                    <div>
                      <div className="text-[#929292] text-xs mb-1 flex items-center gap-1">
                        <MapPin className="w-3 h-3" />
                        발사 장소
                      </div>
                      <div className="text-[#222222] text-sm font-medium">{launch.launchSite}</div>
                    </div>
                  </div>
                </div>
              ))
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
