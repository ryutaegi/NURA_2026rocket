import { useEffect, useRef, useState } from 'react';
import { X } from 'lucide-react';

interface FindRocketModalProps {
  isOpen: boolean;
  onClose: () => void;
  latitude: number;
  longitude: number;
}

// window 인터페이스에 google.maps 속성을 선언하여 타입스크립트 에러를 방지합니다.
declare global {
    interface Window {
        google: any;
    }
}

export default function FindRocketModal({ isOpen, onClose, latitude, longitude }: FindRocketModalProps) {
  const mapRef = useRef<HTMLDivElement>(null);
  const [isApiLoaded, setIsApiLoaded] = useState(false);
  
  // Google Maps 스크립트를 동적으로 로드하는 useEffect
  useEffect(() => {
    // 스크립트가 이미 로드되었다면 상태만 업데이트합니다.
    if (window.google && window.google.maps) {
      setIsApiLoaded(true);
      return;
    }

    const apiKey = import.meta.env.VITE_GOOGLE_MAPS_API_KEY;
    if (!apiKey) {
        console.error("Google Maps API 키가 없습니다. .env.local 파일에 VITE_GOOGLE_MAPS_API_KEY를 추가하세요.");
        return;
    }

    const script = document.createElement('script');
    script.src = `https://maps.googleapis.com/maps/api/js?key=${apiKey}`;
    script.async = true;
    script.defer = true;
    script.onload = () => {
      setIsApiLoaded(true);
    };
    script.onerror = () => {
      console.error("Google Maps 스크립트를 로드하는 데 실패했습니다.");
    };
    
    document.head.appendChild(script);

  }, []);

  // API가 로드된 후 지도를 초기화하는 useEffect
  useEffect(() => {
    if (!isOpen || !isApiLoaded || !mapRef.current) return;

    const map = new window.google.maps.Map(mapRef.current, {
      center: { lat: latitude, lng: longitude },
      zoom: 15,
      // 고급 마커 및 스타일링을 위해 Map ID를 사용하는 것을 권장합니다.
      mapId: 'NURA_ROCKET_MAP', 
      disableDefaultUI: true,
      zoomControl: true,
    });

    new window.google.maps.Marker({
      position: { lat: latitude, lng: longitude },
      map: map,
      title: '로켓 위치',
    });
  }, [isOpen, latitude, longitude, isApiLoaded]);

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
            {/* API가 로드되는 동안 로딩 메시지를 표시합니다. */}
            {!isApiLoaded && (
                <div className="w-full h-full flex flex-col items-center justify-center text-white">
                    <p>지도를 불러오는 중...</p>
                    <p className="text-sm text-gray-400 mt-2">API 키가 올바른지, 인터넷이 연결되어 있는지 확인하세요.</p>
                </div>
            )}
          </div>
        </div>

        {/* 하단 정보 */}
        <div className="p-4 border-t border-gray-800">
           <div className="flex gap-4 text-sm text-gray-400">
                <div><span className="text-white">위도:</span> {latitude.toFixed(6)}°</div>
                <div><span className="text-white">경도:</span> {longitude.toFixed(6)}°</div>
            </div>
        </div>
      </div>
    </div>
  );
}
