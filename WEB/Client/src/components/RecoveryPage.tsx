import { useState, useEffect, useRef } from 'react';
import { MapPin, Trash2, X, History as HistoryIcon } from 'lucide-react';
import { toast } from "sonner";
import { useWebSocket } from '../hooks/useWebSocket';
import { RocketTelemetry, flightPhaseToStageMap } from './MainPage';
import { db } from '../lib/firebase';
import {
  collection,
  addDoc,
  onSnapshot,
  query,
  orderBy,
  deleteDoc,
  doc,
  updateDoc,
  serverTimestamp
} from 'firebase/firestore';

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
  const googleMapRef = useRef<any>(null);
  const recoveryMarkersMapRef = useRef<Map<string, any>>(new Map());
  const [isApiLoaded, setIsApiLoaded] = useState(false);

  // 내 위치 관련
  const [userLocation, setUserLocation] = useState<any>(null);
  const userMarkerRef = useRef<any>(null);

  // 실시간 데이터
  const { lastMessage, isConnected } = useWebSocket();
  const [liveTelemetry, setLiveTelemetry] = useState<RocketTelemetry | null>(null);
  const liveRocketMarkerRef = useRef<any>(null);
  const [isPC, setIsPC] = useState(window.innerWidth >= 1024);

  // 화면 크기 감지
  useEffect(() => {
    const handleResize = () => setIsPC(window.innerWidth >= 1024);
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  // 로딩 상태 제거 (Firestore가 실시간 처리)

  // Firebase 마커 실시간 구독
  useEffect(() => {
    const q = query(collection(db, "recovery_markers"), orderBy("timestamp", "asc"));
    const unsubscribe = onSnapshot(q, (snapshot) => {
      const markersData = snapshot.docs.map(doc => ({
        id: doc.id,
        ...doc.data(),
        timestamp: doc.data().timestamp?.toDate() || new Date()
      })) as RecoveryMarker[];
      setMarkers(markersData);
    }, (error) => {
      console.error("Firestore Subscribe Error:", error);
    });

    return () => unsubscribe();
  }, []);

  // Google Maps 스크립트 로드 - 중복 방지 및 최적화
  useEffect(() => {
    if (window.google?.maps) {
      setIsApiLoaded(true);
      return;
    }

    const apiKey = import.meta.env.VITE_GOOGLE_MAPS_API_KEY;
    if (!apiKey) return;

    if (document.querySelector('script[src*="maps.googleapis.com"]')) {
      // 이미 로드 중인 경우 감시
      const timer = setInterval(() => {
        if (window.google?.maps) {
          setIsApiLoaded(true);
          clearInterval(timer);
        }
      }, 500);
      return;
    }

    const script = document.createElement('script');
    script.src = `https://maps.googleapis.com/maps/api/js?key=${apiKey}&libraries=geometry,drawing,places`;
    script.async = true;
    script.defer = true;
    script.onload = () => setIsApiLoaded(true);
    document.head.appendChild(script);
  }, []);

  // 텔레메트리 업데이트 (로컬)
  useEffect(() => {
    if (lastMessage?.type === 'telemetry') {
      const data = lastMessage.data;
      setLiveTelemetry({
        ...data,
        stage: flightPhaseToStageMap[data.flightPhase] || 'pre-launch',
      });
    }
  }, [lastMessage]);

  // Firebase 실시간 중계 문서 구독 (원격)
  useEffect(() => {
    if (!isConnected) {
      const unsub = onSnapshot(doc(db, "live", "current"), { includeMetadataChanges: true }, (snapshot) => {
        if (snapshot.exists()) {
          const data = snapshot.data({ serverTimestamps: 'estimate' });
          let updatedAt = 0;
          if (data.serverTimestamp?.toDate) {
            updatedAt = data.serverTimestamp.toDate().getTime();
          } else if (data.serverTimestamp?.seconds) {
            updatedAt = data.serverTimestamp.seconds * 1000;
          }

          const timeDiff = Math.abs(Date.now() - updatedAt);
          if (updatedAt && timeDiff < 60000) {
            const telemetry = data.telemetry;
            setLiveTelemetry({
              ...telemetry,
              stage: flightPhaseToStageMap[telemetry.flightPhase] || 'pre-launch',
            });
          }
        }
      }, (err) => console.error("Recovery Remote Sub Error:", err));
      return () => unsub();
    }
  }, [isConnected]);

  // 지도 초기화 - 핵심 로직 집중
  useEffect(() => {
    if (!isApiLoaded || !mapRef.current || googleMapRef.current) return;

    try {
      console.log("Map initialization started on container:", mapRef.current);
      const map = new window.google.maps.Map(mapRef.current, {
        center: { lat: 37.5665, lng: 126.9780 },
        zoom: 13,
        mapTypeControl: true,
        streetViewControl: false,
        fullscreenControl: true,
        backgroundColor: '#0f172a'
      });

      googleMapRef.current = map;

      map.addListener('click', (e: any) => {
        if (e.latLng) {
          // click handler will use current isConnected via a ref or direct access if inside primitive effect
          // However, for clean logic, we'll store a ref for isConnected or use a functional update approach
          // Here, we just call the addMarker function
        }
      });
    } catch (err) {
      console.error("CRITICAL Map Init Error:", err);
    }
  }, [isApiLoaded]);

  // 내 위치 추적 및 권한 대응
  useEffect(() => {
    if (!navigator.geolocation) return;

    const watchId = navigator.geolocation.watchPosition(
      (pos) => {
        setUserLocation({
          lat: pos.coords.latitude,
          lng: pos.coords.longitude,
        });
      },
      (err) => {
        console.log("Geolocation Error:", err.message);
        if (err.code === 1) {
          toast.error("위치 정보 권한이 거부되었습니다. 브라우저 설정에서 위치 권한을 허용해 주세요.");
        }
      },
      { enableHighAccuracy: true, timeout: 10000 }
    );

    return () => navigator.geolocation.clearWatch(watchId);
  }, []);

  // 이펙트를 통해 지도 클릭 리스너를 isConnected 상태와 동기화
  useEffect(() => {
    if (!googleMapRef.current) return;

    const clickListener = googleMapRef.current.addListener('click', (e: any) => {
      if (isConnected && e.latLng) {
        addMarker(e.latLng.lat(), e.latLng.lng());
      } else if (!isConnected) {
        toast.info("마커를 추가하려면 로컬 서버에 연결되어야 합니다.");
      }
    });

    return () => window.google?.maps?.event?.removeListener(clickListener);
  }, [isConnected, isApiLoaded]);

  // 마커 동기화 및 실시간 업데이트 로직 (생략 방지)
  useEffect(() => {
    if (!googleMapRef.current || !isApiLoaded) return;

    // 내 위치 마커
    if (userLocation) {
      if (!userMarkerRef.current) {
        userMarkerRef.current = new window.google.maps.Marker({
          position: userLocation,
          map: googleMapRef.current,
          icon: {
            path: window.google.maps.SymbolPath.CIRCLE,
            fillColor: '#4285F4',
            fillOpacity: 1,
            strokeColor: 'white',
            strokeWeight: 2,
            scale: 7,
          }
        });
      } else {
        userMarkerRef.current.setPosition(userLocation);
      }
    }

    // 목록 마커 동기화
    const currentMarkerIds = new Set(markers.map(m => m.id));
    recoveryMarkersMapRef.current.forEach((marker, id) => {
      if (!currentMarkerIds.has(id)) {
        marker.setMap(null);
        recoveryMarkersMapRef.current.delete(id);
      }
    });

    markers.forEach((markerData, index) => {
      if (!recoveryMarkersMapRef.current.has(markerData.id)) {
        const marker = new window.google.maps.Marker({
          position: { lat: markerData.latitude, lng: markerData.longitude },
          map: googleMapRef.current,
          label: { text: (index + 1).toString(), color: 'white' },
        });
        marker.addListener('click', () => setSelectedMarker(markerData));
        recoveryMarkersMapRef.current.set(markerData.id, marker);
      }
    });

    // 로켓 실시간 마커
    if (liveTelemetry) {
      const pos = { lat: liveTelemetry.latitude, lng: liveTelemetry.longitude };
      if (!liveRocketMarkerRef.current) {
        liveRocketMarkerRef.current = new window.google.maps.Marker({
          position: pos,
          map: googleMapRef.current,
          icon: {
            path: 'M15,0 L10,5 L10,15 L5,20 L5,25 L10,30 L10,40 L15,45 L20,40 L20,30 L25,25 L25,20 L20,15 L20,5 Z',
            fillColor: '#ef4444',
            fillOpacity: 1,
            strokeWeight: 1,
            rotation: liveTelemetry.yaw,
            scale: 0.7,
            anchor: new window.google.maps.Point(15, 25),
          },
          zIndex: 1000
        });
      } else {
        liveRocketMarkerRef.current.setPosition(pos);
        const icon = liveRocketMarkerRef.current.getIcon();
        if (icon) {
          icon.rotation = liveTelemetry.yaw;
          liveRocketMarkerRef.current.setIcon(icon);
        }
      }
    }
  }, [isApiLoaded, userLocation, markers, liveTelemetry]);

  // 핸들러 함수들 (Firebase 연동)
  const addMarker = async (lat: number, lng: number) => {
    if (!isConnected) return;
    try {
      await addDoc(collection(db, "recovery_markers"), {
        latitude: lat,
        longitude: lng,
        notes: '',
        timestamp: serverTimestamp()
      });
      toast.success("포인트가 추가되었습니다.");
    } catch (e) {
      toast.error("포인트 추가 실패");
    }
  };

  const deleteMarker = async (id: string) => {
    if (!isConnected) return;
    try {
      await deleteDoc(doc(db, "recovery_markers", id));
      if (selectedMarker?.id === id) setSelectedMarker(null);
      toast.success("포인트가 삭제되었습니다.");
    } catch (e) {
      toast.error("삭제 실패");
    }
  };

  const updateMarkerNotes = async (id: string, notes: string) => {
    if (!isConnected) return;
    try {
      await updateDoc(doc(db, "recovery_markers", id), { notes });
    } catch (e) {
      console.error("Notes Update Error:", e);
    }
  };

  return (
    <div
      className="h-[calc(100vh-4rem)] p-4 bg-black overflow-hidden flex"
      style={{ flexDirection: isPC ? 'row' : 'column', gap: '1rem' }}
    >
      {/* 맵 컨테이너 */}
      <div
        className="bg-gray-950 rounded-2xl overflow-hidden relative border border-white/10 shadow-2xl"
        style={{ flex: isPC ? 3 : 'none', height: isPC ? '100%' : '40vh' }}
      >
        <div ref={mapRef} style={{ width: '100%', height: '100%' }} />

        {/* 상단 오버레이 제거됨 */}
        {!isApiLoaded && (
          <div className="absolute top-4 left-4 bg-black/80 backdrop-blur-md text-white px-4 py-2 rounded-xl z-10 border border-white/10">
            <p className="text-[10px] text-blue-400 animate-pulse">Loading Map Engine...</p>
          </div>
        )}

        {/* 선택된 마커 정보 오버레이 이동됨 (사이드바로) */}

        {/* 위치 버튼 */}
        {userLocation && (
          <button
            onClick={() => googleMapRef.current?.panTo(userLocation)}
            className="absolute bottom-6 right-6 bg-blue-600 text-white p-4 rounded-full shadow-2xl z-20 hover:bg-blue-500 active:scale-90 transition-all border border-white/20"
          >
            <MapPin className="w-6 h-6" />
          </button>
        )}
      </div>

      {/* 조작 패널 */}
      <div
        className="bg-gray-900/50 backdrop-blur-xl p-4 flex flex-col gap-4 rounded-2xl border border-white/10 overflow-hidden"
        style={{ width: isPC ? '320px' : '100%', flex: isPC ? 'none' : 1 }}
      >
        <div className="flex items-center gap-2 text-white font-black text-sm border-b border-white/5 pb-2">
          <HistoryIcon className="w-4 h-4 text-blue-400" /> 포인트 관리
        </div>

        {/* {!isConnected && (
          <div className="bg-blue-600/10 border border-blue-500/20 p-3 rounded-lg flex flex-col gap-1 items-center text-center">
            <span className="text-[10px] text-blue-400 font-black uppercase tracking-widest">Read Only Mode</span>
            <p className="text-[9px] text-white-300/60 leading-tight">로컬 서버에 연결되지 않아<br />조작이 제한됩니다.</p>
          </div>
        )} */}

        <div className="flex-1 overflow-y-auto space-y-2 min-h-0 py-2">
          {markers.length === 0 ? (
            <div className="h-full flex flex-col items-center justify-center opacity-20 gap-2 mb-10">
              <MapPin className="w-10 h-10" />
              <p className="text-[10px] font-black uppercase tracking-widest">No Active Points</p>
            </div>
          ) : (
            markers.map((m, i) => (
              <div
                key={m.id}
                className={`p-3 rounded-xl border transition-all cursor-pointer transform hover:scale-[1.01] active:scale-[0.98] ${selectedMarker?.id === m.id ? 'bg-blue-600/20 border-blue-500/50 shadow-lg' : 'bg-gray-800/50 border-white/5 hover:border-white/20'}`}
                onClick={() => {
                  setSelectedMarker(m);
                  googleMapRef.current?.panTo({ lat: m.latitude, lng: m.longitude });
                  googleMapRef.current?.setZoom(16);
                }}
              >
                <div className="flex justify-between items-center mb-1">
                  <span className="text-white font-black text-xs uppercase tracking-tight">Point {i + 1}</span>
                  {isConnected && (
                    <button onClick={(e) => { e.stopPropagation(); deleteMarker(m.id); }} className="text-gray-500 hover:text-red-400 transition-colors">
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  )}
                </div>
                <div className="text-[10px] text-gray-500 font-mono">
                  {m.latitude.toFixed(6)}, {m.longitude.toFixed(6)}
                </div>
              </div>
            ))
          )}
        </div>

        {/* 선택된 마커 상세 정보 (사이드바 하단) */}
        {selectedMarker && (
          <div className="mt-auto border-t border-white/5 pt-4 flex flex-col gap-3">
            <div className="flex justify-between items-center">
              <h3 className="font-black text-[10px] uppercase tracking-widest text-blue-400">Point Details</h3>
              <button onClick={() => setSelectedMarker(null)} className="text-gray-500 hover:text-white transition-colors">
                <X className="w-3.5 h-3.5" />
              </button>
            </div>
            <div className="bg-gray-950/30 rounded-xl p-3 border border-white/5">
              <div className="text-[9px] text-gray-500 font-mono mb-2 break-all">
                {selectedMarker.latitude.toFixed(8)}, {selectedMarker.longitude.toFixed(8)}
              </div>
              <textarea
                value={selectedMarker.notes}
                onChange={(e) => {
                  if (!isConnected) return;
                  const newNotes = e.target.value;
                  setSelectedMarker(prev => prev ? { ...prev, notes: newNotes } : null);
                  updateMarkerNotes(selectedMarker.id, newNotes);
                }}
                readOnly={!isConnected}
                className={`w-full bg-gray-950/50 text-white text-[11px] rounded-lg p-3 outline-none h-32 resize-none border transition-all ${!isConnected ? 'border-transparent cursor-default' : 'border-white/10 focus:border-blue-500/50 shadow-inner'}`}
                placeholder={isConnected ? "회수 지점에 대한 메모를 입력하세요..." : "로컬 서버 연결 후 입력 가능합니다."}
              />
              {!isConnected && <p className="text-[8px] text-gray-500 mt-2 italic">* 로컬 서버에 연결되어야 메모를 수정할 수 있습니다.</p>}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
