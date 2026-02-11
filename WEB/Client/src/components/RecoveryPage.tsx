import { useState, useEffect, useRef } from 'react';
import { MapPin, Trash2, Save, Rocket, Download, Upload } from 'lucide-react';
import { toast } from "sonner";
import { useWebSocket } from '../hooks/useWebSocket';
import { RocketTelemetry, flightPhaseToStageMap } from './MainPage'; // MainPage에서 타입과 맵을 가져옵니다.

// window 인터페이스에 google.maps 속성을 선언하여 타입스크립트 에러를 방지합니다.
declare global {
    interface Window {
        google: any;
    }
}

interface RecoveryMarker {
  id: string;
  latitude: number;
  longitude: number;
  timestamp: Date;
  notes: string;
}

export default function RecoveryPage() {
  const [markers, setMarkers] = useState<RecoveryMarker[]>([]);
  const [selectedMarker, setSelectedMarker] = useState<RecoveryMarker | null>(null);
  const mapRef = useRef<HTMLDivElement>(null);
  const googleMapRef = useRef<google.maps.Map | null>(null);
  const recoveryMarkersMapRef = useRef<Map<string, google.maps.Marker>>(new Map());
  const [isApiLoaded, setIsApiLoaded] = useState(false);

  // 실시간 데이터 수신을 위한 WebSocket 훅
  const { lastMessage } = useWebSocket();
  const [liveTelemetry, setLiveTelemetry] = useState<RocketTelemetry | null>(null);
  const liveRocketMarkerRef = useRef<google.maps.Marker | null>(null);

  // 마커 파일 저장/불러오기 관련 상태
  const [savedFiles, setSavedFiles] = useState<string[]>([]);
  const [isLoading, setIsLoading] = useState(false);
  const [selectedFile, setSelectedFile] = useState<string>('');

  const fetchSavedFiles = async () => {
    try {
      // API 경로 수정: http://localhost:3001
      const response = await fetch('http://localhost:3001/api/recovery-markers');
      if (!response.ok) throw new Error('서버에서 파일 목록을 가져오는 데 실패했습니다.');
      const files = await response.json();
      setSavedFiles(files);
      if (files.length > 0) {
        setSelectedFile(files[0]);
      }
    } catch (error: any) {
      toast.error(error.message);
    }
  };

  useEffect(() => {
    fetchSavedFiles();
  }, []);

  // Google Maps 스크립트 로드
  useEffect(() => {
    if (window.google && window.google.maps) {
      setIsApiLoaded(true);
      return;
    }
    const apiKey = import.meta.env.VITE_GOOGLE_MAPS_API_KEY;
    if (!apiKey) {
      console.error("VITE_GOOGLE_MAPS_API_KEY가 .env.local 파일에 필요합니다.");
      return;
    }
    const script = document.createElement('script');
    script.src = `https://maps.googleapis.com/maps/api/js?key=${apiKey}`;
    script.async = true;
    script.defer = true;
    script.onload = () => setIsApiLoaded(true);
    document.head.appendChild(script);
  }, []);
  
  // WebSocket 메시지 처리하여 실시간 텔레메트리 업데이트
  useEffect(() => {
    if (lastMessage && lastMessage.type === 'telemetry') {
      const data = lastMessage.data;
      setLiveTelemetry({
        ...data,
        stage: flightPhaseToStageMap[data.flightPhase] || 'pre-launch',
      });
    }
  }, [lastMessage]);

  // 지도 초기화 및 클릭 리스너 설정
  useEffect(() => {
    if (!isApiLoaded || !mapRef.current || googleMapRef.current) return;

    const map = new window.google.maps.Map(mapRef.current, {
      center: { lat: 37.5665, lng: 126.9780 },
      zoom: 10,
      mapId: 'NURA_ROCKET_RECOVERY_MAP',
      disableDefaultUI: false,
      zoomControl: true,
    });
    googleMapRef.current = map;

    map.addListener('click', (e: google.maps.MapMouseEvent) => {
      if (e.latLng) {
        const newMarker: RecoveryMarker = {
          id: Date.now().toString(),
          latitude: e.latLng.lat(),
          longitude: e.latLng.lng(),
          timestamp: new Date(),
          notes: '',
        };
        setMarkers(prev => [...prev, newMarker]);
        setSelectedMarker(newMarker);
      }
    });
  }, [isApiLoaded]);

  // 'markers' 상태와 지도 위 회수 지점 마커 동기화
  useEffect(() => {
    if (!googleMapRef.current || !isApiLoaded) return;
    
    const currentMarkerIds = new Set(markers.map(m => m.id));

    // 삭제된 마커를 지도에서 제거
    recoveryMarkersMapRef.current.forEach((marker, id) => {
      if (!currentMarkerIds.has(id)) {
        marker.setMap(null);
        recoveryMarkersMapRef.current.delete(id);
      }
    });

    // 추가/업데이트된 마커를 지도에 렌더링
    markers.forEach((markerData, index) => {
      if (!recoveryMarkersMapRef.current.has(markerData.id)) {
        const newMarker = new window.google.maps.Marker({
          position: { lat: markerData.latitude, lng: markerData.longitude },
          map: googleMapRef.current,
          title: `지점 ${index + 1}`,
        });
        newMarker.addListener('click', () => setSelectedMarker(markerData));
        recoveryMarkersMapRef.current.set(markerData.id, newMarker);
      }
    });
  }, [isApiLoaded, markers]);

  // 선택된 마커 아이콘 업데이트
  useEffect(() => {
    recoveryMarkersMapRef.current.forEach((markerInstance, id) => {
      const isSelected = selectedMarker?.id === id;
      const markerData = markers.find(m => m.id === id);
      const index = markers.findIndex(m => m.id === id);

      markerInstance.setLabel({
        text: (index + 1).toString(),
        color: 'white',
      });
      markerInstance.setIcon({
        path: window.google.maps.SymbolPath.CIRCLE,
        fillColor: isSelected ? '#3b82f6' : '#6b7280',
        fillOpacity: 1,
        strokeWeight: 0,
        scale: 10,
      });
    });
  }, [isApiLoaded, selectedMarker, markers]);

  // 실시간 로켓 마커 업데이트
  useEffect(() => {
    if (!googleMapRef.current || !isApiLoaded || !liveTelemetry) return;
    
    const livePosition = { lat: liveTelemetry.latitude, lng: liveTelemetry.longitude };

    if (!liveRocketMarkerRef.current) {
      const liveIcon = {
        path: 'M15,0 L10,5 L10,15 L5,20 L5,25 L10,30 L10,40 L15,45 L20,40 L20,30 L25,25 L25,20 L20,15 L20,5 Z',
        fillColor: '#ef4444', // 빨간색
        fillOpacity: 1,
        strokeWeight: 1,
        rotation: liveTelemetry.yaw,
        scale: 0.7,
        anchor: new window.google.maps.Point(15, 25),
      };
      liveRocketMarkerRef.current = new window.google.maps.Marker({
        position: livePosition,
        map: googleMapRef.current,
        icon: liveIcon,
        title: '실시간 로켓 위치',
        zIndex: 1000, // 다른 마커들보다 위에 표시
      });
    } else {
      liveRocketMarkerRef.current.setPosition(livePosition);
      const icon = liveRocketMarkerRef.current.getIcon() as google.maps.Symbol;
      if (icon) {
        icon.rotation = liveTelemetry.yaw;
        liveRocketMarkerRef.current.setIcon(icon);
      }
    }
  }, [isApiLoaded, liveTelemetry]);

  const deleteMarker = (id: string) => {
    setMarkers(prev => prev.filter(m => m.id !== id));
    if (selectedMarker?.id === id) {
      setSelectedMarker(null);
    }
  };

  const updateMarkerNotes = (id: string, notes: string) => {
    setMarkers(prev => prev.map(m => m.id === id ? { ...m, notes } : m));
    if (selectedMarker?.id === id) {
      setSelectedMarker(prev => prev ? { ...prev, notes } : null);
    }
  };

  const handleSaveMarkers = async () => {
    if (markers.length === 0) {
      toast.error("저장할 마커가 없습니다.");
      return;
    }
    setIsLoading(true);
    try {
      const response = await fetch('http://localhost:3001/api/recovery-markers', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ markers }),
      });
      if (!response.ok) throw new Error('마커 저장에 실패했습니다.');
      const result = await response.json();
      toast.success(`마커가 ${result.filename}으로 저장되었습니다.`);
      setMarkers([]); // 현재 마커 지우기
      setSelectedMarker(null); // 선택된 마커 초기화
      await fetchSavedFiles(); // 파일 목록 새로고침
    } catch (error: any) {
      toast.error(error.message);
    } finally {
      setIsLoading(false);
    }
  };

  const handleLoadMarkers = async () => {
    if (!selectedFile) {
      toast.error("불러올 파일을 선택하세요.");
      return;
    }
    setIsLoading(true);
    try {
      const response = await fetch(`http://localhost:3001/api/recovery-markers/${selectedFile}`);
      if (!response.ok) throw new Error('마커 불러오기에 실패했습니다.');
      const loadedMarkers = await response.json();
      
      // JSON으로 직렬화된 timestamp를 다시 Date 객체로 변환
      const markersWithDate = loadedMarkers.map((m: any) => ({ ...m, timestamp: new Date(m.timestamp) }));

      setMarkers(markersWithDate);
      setSelectedMarker(null);
      toast.success(`${selectedFile}에서 마커를 불러왔습니다.`);
    } catch (error: any) {
      toast.error(error.message);
    } finally {
      setIsLoading(false);
    }
  };

  const handleDeleteFile = async () => {
    if (!selectedFile) {
      toast.error("삭제할 파일을 선택하세요.");
      return;
    }

    if (!window.confirm(`정말로 '${selectedFile}' 파일을 삭제하시겠습니까? 이 작업은 되돌릴 수 없습니다.`)) {
      return;
    }

    setIsLoading(true);
    try {
      const response = await fetch(`http://localhost:3001/api/recovery-markers/${selectedFile}`, {
        method: 'DELETE',
      });
      if (!response.ok) throw new Error('파일 삭제에 실패했습니다.');
      
      toast.success(`'${selectedFile}' 파일이 삭제되었습니다.`);
      
      setMarkers([]); // 현재 마커 지우기
      setSelectedMarker(null);
      await fetchSavedFiles(); // 파일 목록 새로고침

    } catch (error: any) {
      toast.error(error.message);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="h-[calc(100vh-4rem)] p-4">
      <div className="grid grid-cols-1 lg:grid-cols-4 gap-4 h-full">
        <div className="lg:col-span-3 bg-gray-900 rounded-lg overflow-hidden relative">
          <div ref={mapRef} className="w-full h-full relative">
            {!isApiLoaded && (
              <div className="w-full h-full flex flex-col items-center justify-center text-white">
                <p>지도를 불러오는 중...</p>
              </div>
            )}
            <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-3 rounded-lg z-10">
              <div className="flex items-center gap-2 mb-2">
                <MapPin className="w-5 h-5 text-blue-400" />
                <span className="text-sm">로켓 회수 지점 관리</span>
              </div>
              <div className="text-xs text-gray-400">지도를 클릭하여 회수 지점 마커를 추가하세요</div>
              {liveTelemetry && (
                <div className="flex items-center gap-2 mt-4 pt-2 border-t border-gray-600">
                    <Rocket className="w-5 h-5 text-red-400 animate-pulse" />
                    <span className="text-sm">실시간 로켓 추적 중</span>
                </div>
              )}
            </div>
          </div>
        </div>
        <div className="bg-gray-900 rounded-lg p-4 flex flex-col overflow-hidden">
          <div className="flex items-center justify-between mb-4">
            <h2 className="text-white flex items-center gap-2"><MapPin className="w-5 h-5" />회수 지점 목록</h2>
          </div>
          
          <div className="bg-gray-800 rounded-lg p-3 mb-4">
            <div className="grid grid-cols-1 gap-2">
              <select
                value={selectedFile}
                onChange={(e) => setSelectedFile(e.target.value)}
                className="w-full bg-gray-900 text-white text-sm rounded px-3 py-2 border border-gray-700 focus:border-blue-500 focus:outline-none"
                disabled={isLoading || savedFiles.length === 0}
              >
                {savedFiles.length === 0 ? (
                  <option value="">저장된 파일 없음</option>
                ) : (
                  savedFiles.map(file => <option key={file} value={file}>{file}</option>)
                )}
              </select>
              <div className="mt-2 grid grid-cols-1 gap-2"> {/* Reverted to grid-cols-1 for vertical stacking */}
                <button 
                  onClick={handleLoadMarkers}
                  className="w-full bg-blue-600 hover:bg-blue-700 text-white text-sm px-4 py-2 rounded transition-colors flex items-center justify-center gap-2 disabled:bg-gray-600"
                  disabled={isLoading || !selectedFile}
                >
                  <Download className="w-4 h-4" />
                  불러오기
                </button>
                <button 
                  onClick={handleDeleteFile}
                  className="w-full bg-red-600 hover:bg-red-700 text-white text-sm px-4 py-2 rounded transition-colors flex items-center justify-center gap-2 disabled:bg-gray-600"
                  disabled={isLoading || !selectedFile}
                >
                  <Trash2 className="w-4 h-4" />
                  삭제
                </button>
                <button 
                  onClick={handleSaveMarkers}
                  className="w-full bg-green-600 hover:bg-green-700 text-white text-sm px-4 py-2 rounded transition-colors flex items-center justify-center gap-2 disabled:bg-gray-600"
                  disabled={isLoading || markers.length === 0}
                >
                  <Upload className="w-4 h-4" />
                  저장
                </button>
              </div>
            </div>
          </div>

          <div className="flex-1 space-y-2 mb-4 overflow-y-auto min-h-0">
            {markers.length === 0 ? (
              <div className="text-gray-500 text-sm text-center py-8">지도를 클릭하여 마커를 추가하거나<br/>저장된 마커를 불러오세요.</div>
            ) : (
              markers.map((marker, index) => (
                <div key={marker.id} className={`bg-gray-800 rounded-lg p-3 cursor-pointer transition-all ${selectedMarker?.id === marker.id ? 'ring-2 ring-blue-500' : 'hover:bg-gray-750'}`} onClick={() => setSelectedMarker(marker)}>
                  <div className="flex items-start justify-between mb-2">
                    <div className="flex items-center gap-2">
                      <div className="w-6 h-6 bg-blue-600 rounded-full flex items-center justify-center text-white text-xs">{index + 1}</div>
                      <span className="text-white text-sm">지점 {index + 1}</span>
                    </div>
                    <button onClick={(e) => { e.stopPropagation(); deleteMarker(marker.id); }} className="text-gray-400 hover:text-red-400 transition-colors"><Trash2 className="w-4 h-4" /></button>
                  </div>
                  <div className="text-xs text-gray-400 space-y-1">
                    <div>위도: {marker.latitude.toFixed(6)}°</div>
                    <div>경도: {marker.longitude.toFixed(6)}°</div>
                    <div>시간: {new Date(marker.timestamp).toLocaleTimeString()}</div>
                  </div>
                  {marker.notes && <div className="mt-2 text-xs text-gray-300 bg-gray-900 rounded p-2">{marker.notes}</div>}
                </div>
              ))
            )}
          </div>
          {selectedMarker && (
            <div className="bg-gray-800 rounded-lg p-4 border-t border-gray-700">
              <h3 className="text-white text-sm mb-2">메모</h3>
              <textarea value={selectedMarker.notes} onChange={(e) => updateMarkerNotes(selectedMarker.id, e.target.value)} placeholder="회수 지점에 대한 메모를 입력하세요..." className="w-full bg-gray-900 text-white text-sm rounded px-3 py-2 border border-gray-700 focus:border-blue-500 focus:outline-none resize-none" rows={3}/>
              <button onClick={() => { updateMarkerNotes(selectedMarker.id, selectedMarker.notes); setSelectedMarker(null); }} className="mt-2 w-full bg-blue-600 hover:bg-blue-700 text-white text-sm px-4 py-2 rounded transition-colors flex items-center justify-center gap-2"><Save className="w-4 h-4" />저장</button>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
