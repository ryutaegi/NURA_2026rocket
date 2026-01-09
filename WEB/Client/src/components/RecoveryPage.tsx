import { useState, useEffect, useRef } from 'react';
import { MapPin, Trash2, Save } from 'lucide-react';

interface RecoveryMarker {
  id: string;
  latitude: number;
  longitude: number;
  timestamp: Date;
  notes: string;
}

export default function RecoveryPage() {
  const [markers, setMarkers] = useState<RecoveryMarker[]>([
    {
      id: '1',
      latitude: 37.5665,
      longitude: 126.9780,
      timestamp: new Date(),
      notes: '예상 낙하 지점',
    },
  ]);
  const [selectedMarker, setSelectedMarker] = useState<RecoveryMarker | null>(null);
  const mapRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    // Google Maps API 초기화
    // 실제 사용 시: Google Maps API 키 필요
  }, []);

  const handleMapClick = (e: React.MouseEvent<HTMLDivElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    
    // 클릭 위치를 좌표로 변환 (단순화)
    const latitude = 37.5665 + (0.5 - y / rect.height) * 0.1;
    const longitude = 126.9780 + (x / rect.width - 0.5) * 0.1;

    const newMarker: RecoveryMarker = {
      id: Date.now().toString(),
      latitude,
      longitude,
      timestamp: new Date(),
      notes: '',
    };

    setMarkers([...markers, newMarker]);
    setSelectedMarker(newMarker);
  };

  const deleteMarker = (id: string) => {
    setMarkers(markers.filter(m => m.id !== id));
    if (selectedMarker?.id === id) {
      setSelectedMarker(null);
    }
  };

  const updateMarkerNotes = (id: string, notes: string) => {
    setMarkers(markers.map(m => m.id === id ? { ...m, notes } : m));
    if (selectedMarker?.id === id) {
      setSelectedMarker({ ...selectedMarker, notes });
    }
  };

  return (
    <div className="h-[calc(100vh-4rem)] p-4">
      <div className="grid grid-cols-1 lg:grid-cols-4 gap-4 h-full">
        {/* 지도 영역 */}
        <div className="lg:col-span-3 bg-gray-900 rounded-lg overflow-hidden relative">
          <div
            ref={mapRef}
            onClick={handleMapClick}
            className="w-full h-full cursor-crosshair bg-gray-800 relative"
          >
            {/* 배경 */}
            <div className="absolute inset-0 bg-gradient-to-br from-gray-700 to-gray-900" />
            
            {/* 그리드 */}
            <div className="absolute inset-0" style={{
              backgroundImage: `
                linear-gradient(rgba(59, 130, 246, 0.05) 1px, transparent 1px),
                linear-gradient(90deg, rgba(59, 130, 246, 0.05) 1px, transparent 1px)
              `,
              backgroundSize: '50px 50px'
            }} />

            {/* 마커들 */}
            {markers.map((marker, index) => (
              <div
                key={marker.id}
                className="absolute cursor-pointer transform -translate-x-1/2 -translate-y-full transition-all hover:scale-110"
                style={{
                  left: `${((marker.longitude - 126.9780) / 0.1 + 0.5) * 100}%`,
                  top: `${(0.5 - (marker.latitude - 37.5665) / 0.1) * 100}%`,
                }}
                onClick={(e) => {
                  e.stopPropagation();
                  setSelectedMarker(marker);
                }}
              >
                {/* 펄스 효과 */}
                <div className="absolute inset-0 -m-4">
                  <div className="w-12 h-12 bg-blue-500/20 rounded-full animate-ping" />
                </div>
                
                {/* 마커 */}
                <div className="relative z-10">
                  <svg width="40" height="50" viewBox="0 0 40 50" fill="none">
                    <path
                      d="M20 0C11.716 0 5 6.716 5 15c0 8.284 15 35 15 35s15-26.716 15-35C35 6.716 28.284 0 20 0z"
                      fill={selectedMarker?.id === marker.id ? '#3b82f6' : '#6b7280'}
                      className="transition-colors"
                    />
                    <text
                      x="20"
                      y="18"
                      textAnchor="middle"
                      className="text-sm fill-white"
                    >
                      {index + 1}
                    </text>
                  </svg>
                </div>
              </div>
            ))}

            {/* 안내 */}
            <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-3 rounded-lg">
              <div className="flex items-center gap-2 mb-2">
                <MapPin className="w-5 h-5 text-blue-400" />
                <span className="text-sm">로켓 회수 지점 관리</span>
              </div>
              <div className="text-xs text-gray-400">
                지도를 클릭하여 회수 지점 마커를 추가하세요
              </div>
              <div className="text-xs text-gray-400 mt-1">
                Google Maps API 키 필요 (실제 환경)
              </div>
            </div>

            {/* 줌 컨트롤 */}
            <div className="absolute top-4 right-4 bg-black/70 backdrop-blur-sm rounded-lg overflow-hidden">
              <button className="block w-10 h-10 text-white hover:bg-white/10 transition-colors border-b border-gray-700">
                +
              </button>
              <button className="block w-10 h-10 text-white hover:bg-white/10 transition-colors">
                −
              </button>
            </div>
          </div>
        </div>

        {/* 사이드바 */}
        <div className="bg-gray-900 rounded-lg p-4 flex flex-col">
          <h2 className="text-white mb-4 flex items-center gap-2">
            <MapPin className="w-5 h-5" />
            회수 지점 목록
          </h2>

          {/* 마커 리스트 */}
          <div className="flex-1 overflow-y-auto space-y-2 mb-4">
            {markers.length === 0 ? (
              <div className="text-gray-500 text-sm text-center py-8">
                지도를 클릭하여<br />마커를 추가하세요
              </div>
            ) : (
              markers.map((marker, index) => (
                <div
                  key={marker.id}
                  className={`bg-gray-800 rounded-lg p-3 cursor-pointer transition-all ${
                    selectedMarker?.id === marker.id ? 'ring-2 ring-blue-500' : 'hover:bg-gray-750'
                  }`}
                  onClick={() => setSelectedMarker(marker)}
                >
                  <div className="flex items-start justify-between mb-2">
                    <div className="flex items-center gap-2">
                      <div className="w-6 h-6 bg-blue-600 rounded-full flex items-center justify-center text-white text-xs">
                        {index + 1}
                      </div>
                      <span className="text-white text-sm">지점 {index + 1}</span>
                    </div>
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        deleteMarker(marker.id);
                      }}
                      className="text-gray-400 hover:text-red-400 transition-colors"
                    >
                      <Trash2 className="w-4 h-4" />
                    </button>
                  </div>
                  <div className="text-xs text-gray-400 space-y-1">
                    <div>위도: {marker.latitude.toFixed(6)}°</div>
                    <div>경도: {marker.longitude.toFixed(6)}°</div>
                    <div>시간: {marker.timestamp.toLocaleTimeString()}</div>
                  </div>
                  {marker.notes && (
                    <div className="mt-2 text-xs text-gray-300 bg-gray-900 rounded p-2">
                      {marker.notes}
                    </div>
                  )}
                </div>
              ))
            )}
          </div>

          {/* 선택된 마커 상세 정보 */}
          {selectedMarker && (
            <div className="bg-gray-800 rounded-lg p-4 border-t border-gray-700">
              <h3 className="text-white text-sm mb-2">메모</h3>
              <textarea
                value={selectedMarker.notes}
                onChange={(e) => updateMarkerNotes(selectedMarker.id, e.target.value)}
                placeholder="회수 지점에 대한 메모를 입력하세요..."
                className="w-full bg-gray-900 text-white text-sm rounded px-3 py-2 border border-gray-700 focus:border-blue-500 focus:outline-none resize-none"
                rows={3}
              />
              <button className="mt-2 w-full bg-blue-600 hover:bg-blue-700 text-white text-sm px-4 py-2 rounded transition-colors flex items-center justify-center gap-2">
                <Save className="w-4 h-4" />
                저장
              </button>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
