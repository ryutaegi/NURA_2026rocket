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

  const [userLocation, setUserLocation] = useState<any>(null);
  const userMarkerRef = useRef<any>(null);

  const { lastMessage, isConnected } = useWebSocket();
  const [liveTelemetry, setLiveTelemetry] = useState<RocketTelemetry | null>(null);
  const liveRocketMarkerRef = useRef<any>(null);
  const [isPC, setIsPC] = useState(window.innerWidth >= 1024);

  useEffect(() => {
    const handleResize = () => setIsPC(window.innerWidth >= 1024);
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

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

  useEffect(() => {
    if (window.google?.maps) {
      setIsApiLoaded(true);
      return;
    }

    const apiKey = import.meta.env.VITE_GOOGLE_MAPS_API_KEY;
    if (!apiKey) return;

    if (document.querySelector('script[src*="maps.googleapis.com"]')) {
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

  useEffect(() => {
    if (lastMessage?.type === 'telemetry') {
      const data = lastMessage.data;
      setLiveTelemetry({
        ...data,
        stage: flightPhaseToStageMap[data.flightPhase] || 'pre-launch',
      });
    }
  }, [lastMessage]);

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

  useEffect(() => {
    if (!isApiLoaded || !mapRef.current || googleMapRef.current) return;

    try {
      const map = new window.google.maps.Map(mapRef.current, {
        center: { lat: 37.5665, lng: 126.9780 },
        zoom: 13,
        mapTypeControl: true,
        streetViewControl: false,
        fullscreenControl: true,
        backgroundColor: '#f7f7f7'
      });

      googleMapRef.current = map;

      map.addListener('click', (_e: any) => {
        // click handler synced via isConnected effect below
      });
    } catch (err) {
      console.error("CRITICAL Map Init Error:", err);
    }
  }, [isApiLoaded]);

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

  useEffect(() => {
    if (!googleMapRef.current || !isApiLoaded) return;

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

    if (liveTelemetry) {
      const pos = { lat: liveTelemetry.latitude, lng: liveTelemetry.longitude };
      if (!liveRocketMarkerRef.current) {
        liveRocketMarkerRef.current = new window.google.maps.Marker({
          position: pos,
          map: googleMapRef.current,
          icon: {
            path: 'M15,0 L10,5 L10,15 L5,20 L5,25 L10,30 L10,40 L15,45 L20,40 L20,30 L25,25 L25,20 L20,15 L20,5 Z',
            fillColor: '#ff385c',
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

  const panelShadow = { boxShadow: 'rgba(0,0,0,0.02) 0 0 0 1px, rgba(0,0,0,0.04) 0 2px 6px 0, rgba(0,0,0,0.1) 0 4px 8px 0' };

  return (
    <div
      className="h-[calc(100vh-4rem)] p-4 bg-[#f7f7f7] overflow-hidden flex"
      style={{ flexDirection: isPC ? 'row' : 'column', gap: '1rem' }}
    >
      {/* 맵 컨테이너 */}
      <div
        className="bg-white rounded-2xl overflow-hidden relative border border-[#dddddd]"
        style={{
          flex: isPC ? 3 : 'none',
          height: isPC ? '100%' : '40vh',
          ...panelShadow
        }}
      >
        <div ref={mapRef} style={{ width: '100%', height: '100%' }} />

        {!isApiLoaded && (
          <div className="absolute top-4 left-4 bg-white/90 backdrop-blur-md text-[#222222] px-4 py-2 rounded-xl z-10 border border-[#dddddd]">
            <p className="text-[10px] text-[#ff385c] animate-pulse font-semibold">Loading Map Engine...</p>
          </div>
        )}

        {userLocation && (
          <button
            onClick={() => googleMapRef.current?.panTo(userLocation)}
            className="absolute bottom-6 right-6 bg-[#ff385c] hover:bg-[#e00b41] text-white p-4 rounded-full z-20 active:scale-90 transition-all border-2 border-white"
            style={{ boxShadow: '0 4px 12px rgba(255,56,92,0.4)' }}
          >
            <MapPin className="w-6 h-6" />
          </button>
        )}
      </div>

      {/* 조작 패널 */}
      <div
        className="bg-white p-4 flex flex-col gap-4 rounded-2xl border border-[#dddddd] overflow-hidden"
        style={{
          width: isPC ? '300px' : '100%',
          flex: isPC ? 'none' : 1,
          ...panelShadow
        }}
      >
        <div className="flex items-center gap-2 text-[#222222] font-semibold text-sm border-b border-[#ebebeb] pb-3">
          <HistoryIcon className="w-4 h-4 text-[#ff385c]" />
          포인트 관리
        </div>

        <div className="flex-1 overflow-y-auto space-y-2 min-h-0 py-1">
          {markers.length === 0 ? (
            <div className="h-full flex flex-col items-center justify-center gap-2 mb-10 opacity-30">
              <MapPin className="w-10 h-10 text-[#222222]" />
              <p className="text-[10px] font-semibold uppercase tracking-widest text-[#6a6a6a]">No Active Points</p>
            </div>
          ) : (
            markers.map((m, i) => (
              <div
                key={m.id}
                className={`p-3 rounded-xl border transition-all cursor-pointer ${
                  selectedMarker?.id === m.id
                    ? 'bg-[#fff5f7] border-[#ff385c]/40'
                    : 'bg-white border-[#dddddd] hover:border-[#c1c1c1]'
                }`}
                onClick={() => {
                  setSelectedMarker(m);
                  googleMapRef.current?.panTo({ lat: m.latitude, lng: m.longitude });
                  googleMapRef.current?.setZoom(16);
                }}
              >
                <div className="flex justify-between items-center mb-1">
                  <span className="text-[#222222] font-semibold text-xs">Point {i + 1}</span>
                  {isConnected && (
                    <button
                      onClick={(e) => { e.stopPropagation(); deleteMarker(m.id); }}
                      className="text-[#929292] hover:text-red-500 transition-colors"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  )}
                </div>
                <div className="text-[10px] text-[#6a6a6a] font-mono">
                  {m.latitude.toFixed(6)}, {m.longitude.toFixed(6)}
                </div>
              </div>
            ))
          )}
        </div>

        {selectedMarker && (
          <div className="mt-auto border-t border-[#ebebeb] pt-4 flex flex-col gap-3">
            <div className="flex justify-between items-center">
              <h3 className="font-semibold text-[10px] uppercase tracking-widest text-[#ff385c]">Point Details</h3>
              <button
                onClick={() => setSelectedMarker(null)}
                className="text-[#6a6a6a] hover:text-[#222222] transition-colors"
              >
                <X className="w-3.5 h-3.5" />
              </button>
            </div>
            <div className="bg-[#f7f7f7] rounded-xl p-3 border border-[#dddddd]">
              <div className="text-[9px] text-[#6a6a6a] font-mono mb-2 break-all">
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
                className={`w-full bg-white text-[#222222] text-[11px] rounded-lg p-3 outline-none h-28 resize-none border transition-all ${
                  !isConnected
                    ? 'border-[#dddddd] cursor-default text-[#6a6a6a]'
                    : 'border-[#dddddd] focus:border-[#222222]'
                }`}
                placeholder={isConnected ? "회수 지점에 대한 메모를 입력하세요..." : "로컬 서버 연결 후 입력 가능합니다."}
              />
              {!isConnected && (
                <p className="text-[8px] text-[#929292] mt-1.5 italic">* 로컬 서버에 연결되어야 메모를 수정할 수 있습니다.</p>
              )}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
