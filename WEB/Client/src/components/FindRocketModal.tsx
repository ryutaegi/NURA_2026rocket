import { useEffect, useRef } from 'react';
import { X } from 'lucide-react';

interface FindRocketModalProps {
  isOpen: boolean;
  onClose: () => void;
  latitude: number;
  longitude: number;
}

export default function FindRocketModal({ isOpen, onClose, latitude, longitude }: FindRocketModalProps) {
  const mapRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!isOpen || !mapRef.current) return;

    // Google Maps API 초기화
    // 실제 사용 시: Google Maps API 키 필요
    // const map = new google.maps.Map(mapRef.current, {
    //   center: { lat: latitude, lng: longitude },
    //   zoom: 15,
    // });
    // const marker = new google.maps.Marker({
    //   position: { lat: latitude, lng: longitude },
    //   map: map,
    //   title: '로켓 위치',
    // });
  }, [isOpen, latitude, longitude]);

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 bg-black/80 flex items-center justify-center z-50 p-4">
      <div className="bg-gray-900 rounded-lg w-full max-w-4xl max-h-[90vh] flex flex-col">
        {/* 헤더 */}
        <div className="flex items-center justify-between p-4 border-b border-gray-800">
          <h2 className="text-xl text-white">로켓 위치 찾기</h2>
          <button
            onClick={onClose}
            className="text-gray-400 hover:text-white transition-colors"
          >
            <X className="w-6 h-6" />
          </button>
        </div>

        {/* 지도 영역 */}
        <div className="flex-1 relative">
          <div ref={mapRef} className="absolute inset-0 bg-gray-800">
            {/* Google Maps 플레이스홀더 */}
            <div className="w-full h-full flex flex-col items-center justify-center">
              <div className="relative w-full h-full overflow-hidden">
                {/* 위성 배경 스타일 */}
                <div className="absolute inset-0 bg-gradient-to-br from-gray-700 to-gray-900" />
                
                {/* 그리드 오버레이 */}
                <div className="absolute inset-0" style={{
                  backgroundImage: `
                    linear-gradient(rgba(59, 130, 246, 0.05) 1px, transparent 1px),
                    linear-gradient(90deg, rgba(59, 130, 246, 0.05) 1px, transparent 1px)
                  `,
                  backgroundSize: '50px 50px'
                }} />

                {/* 로켓 마커 */}
                <div className="absolute top-1/2 left-1/2 transform -translate-x-1/2 -translate-y-1/2">
                  <div className="relative">
                    {/* 펄스 효과 */}
                    <div className="absolute inset-0 -m-4">
                      <div className="w-12 h-12 bg-red-500/30 rounded-full animate-ping" />
                    </div>
                    
                    {/* 마커 핀 */}
                    <div className="relative z-10">
                      <svg width="40" height="50" viewBox="0 0 40 50" fill="none">
                        <path
                          d="M20 0C11.716 0 5 6.716 5 15c0 8.284 15 35 15 35s15-26.716 15-35C35 6.716 28.284 0 20 0z"
                          fill="#ef4444"
                        />
                        <circle cx="20" cy="15" r="7" fill="white" />
                      </svg>
                    </div>
                    
                    {/* 정보 박스 */}
                    <div className="absolute -top-16 left-1/2 transform -translate-x-1/2 bg-black/90 backdrop-blur-sm text-white px-4 py-2 rounded-lg whitespace-nowrap">
                      <div className="text-xs text-gray-400">로켓 위치</div>
                      <div className="text-sm">{latitude.toFixed(6)}°, {longitude.toFixed(6)}°</div>
                    </div>
                  </div>
                </div>

                {/* 컨트롤 오버레이 */}
                <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-3 rounded-lg">
                  <div className="text-xs text-gray-400 mb-2">Google Maps</div>
                  <div className="text-sm">위도: {latitude.toFixed(6)}°</div>
                  <div className="text-sm">경도: {longitude.toFixed(6)}°</div>
                  <div className="text-xs text-gray-400 mt-2">
                    실제 환경에서는 Google Maps API 키가 필요합니다
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

                {/* 스케일 */}
                <div className="absolute bottom-4 left-4 bg-black/70 backdrop-blur-sm text-white px-3 py-2 rounded-lg text-xs">
                  <div className="flex items-center gap-2">
                    <div className="w-20 h-0.5 bg-white" />
                    <span>100m</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* 하단 정보 */}
        <div className="p-4 border-t border-gray-800">
          <div className="flex gap-4 text-sm text-gray-400">
            <div>
              <span className="text-white">방위각:</span> {Math.random() * 360 | 0}°
            </div>
            <div>
              <span className="text-white">거리:</span> {(Math.random() * 5000).toFixed(0)}m
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
