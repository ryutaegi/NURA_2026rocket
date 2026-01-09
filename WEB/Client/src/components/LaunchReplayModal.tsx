import { useState, useEffect } from 'react';
import { X, Play, Pause, RotateCcw, FastForward } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';

interface LaunchRecord {
  id: string;
  date: Date;
  maxAltitude: number;
  maxSpeed: number;
  duration: number;
  status: 'success' | 'partial' | 'failed';
  launchSite: string;
  landingCoords: { lat: number; lng: number };
  telemetryData: any[];
}

interface LaunchReplayModalProps {
  isOpen: boolean;
  onClose: () => void;
  launch: LaunchRecord;
}

export default function LaunchReplayModal({ isOpen, onClose, launch }: LaunchReplayModalProps) {
  const [currentTime, setCurrentTime] = useState(0);
  const [isPlaying, setIsPlaying] = useState(false);
  const [playbackSpeed, setPlaybackSpeed] = useState(1);

  useEffect(() => {
    if (!isPlaying) return;

    const interval = setInterval(() => {
      setCurrentTime((prev) => {
        const next = prev + playbackSpeed;
        if (next >= launch.duration) {
          setIsPlaying(false);
          return launch.duration;
        }
        return next;
      });
    }, 100);

    return () => clearInterval(interval);
  }, [isPlaying, playbackSpeed, launch.duration]);

  const handleReset = () => {
    setCurrentTime(0);
    setIsPlaying(false);
  };

  const currentData = launch.telemetryData[Math.floor(currentTime)] || launch.telemetryData[0];
  const chartData = launch.telemetryData.slice(0, Math.floor(currentTime) + 1);

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 bg-black/80 flex items-center justify-center z-50 p-4">
      <div className="bg-gray-900 rounded-lg w-full max-w-6xl max-h-[90vh] flex flex-col">
        {/* 헤더 */}
        <div className="flex items-center justify-between p-4 border-b border-gray-800">
          <div>
            <h2 className="text-xl text-white">발사 리플레이 #{launch.id}</h2>
            <p className="text-sm text-gray-400 mt-1">
              {launch.date.toLocaleString('ko-KR')} - {launch.launchSite}
            </p>
          </div>
          <button
            onClick={onClose}
            className="text-gray-400 hover:text-white transition-colors"
          >
            <X className="w-6 h-6" />
          </button>
        </div>

        {/* 컨텐츠 */}
        <div className="flex-1 overflow-y-auto p-4">
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-4 mb-4">
            {/* 고도 차트 */}
            <div className="bg-gray-800 rounded-lg p-4">
              <h3 className="text-white text-sm mb-3">고도 (m)</h3>
              <ResponsiveContainer width="100%" height={200}>
                <LineChart data={chartData}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
                  <XAxis dataKey="time" stroke="#9ca3af" />
                  <YAxis stroke="#9ca3af" />
                  <Tooltip
                    contentStyle={{ backgroundColor: '#1f2937', border: 'none', borderRadius: '0.5rem' }}
                    labelStyle={{ color: '#9ca3af' }}
                  />
                  <Line type="monotone" dataKey="altitude" stroke="#3b82f6" strokeWidth={2} dot={false} />
                </LineChart>
              </ResponsiveContainer>
            </div>

            {/* 속도 차트 */}
            <div className="bg-gray-800 rounded-lg p-4">
              <h3 className="text-white text-sm mb-3">속도 (m/s)</h3>
              <ResponsiveContainer width="100%" height={200}>
                <LineChart data={chartData}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
                  <XAxis dataKey="time" stroke="#9ca3af" />
                  <YAxis stroke="#9ca3af" />
                  <Tooltip
                    contentStyle={{ backgroundColor: '#1f2937', border: 'none', borderRadius: '0.5rem' }}
                    labelStyle={{ color: '#9ca3af' }}
                  />
                  <Line type="monotone" dataKey="speed" stroke="#8b5cf6" strokeWidth={2} dot={false} />
                </LineChart>
              </ResponsiveContainer>
            </div>
          </div>

          {/* 현재 데이터 */}
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4 mb-4">
            <div className="bg-gray-800 rounded-lg p-4">
              <div className="text-gray-400 text-xs mb-1">고도</div>
              <div className="text-blue-400 text-xl">{currentData.altitude.toFixed(0)} m</div>
            </div>
            <div className="bg-gray-800 rounded-lg p-4">
              <div className="text-gray-400 text-xs mb-1">속도</div>
              <div className="text-purple-400 text-xl">{currentData.speed.toFixed(1)} m/s</div>
            </div>
            <div className="bg-gray-800 rounded-lg p-4">
              <div className="text-gray-400 text-xs mb-1">단계</div>
              <div className="text-green-400 text-sm capitalize">{currentData.stage}</div>
            </div>
            <div className="bg-gray-800 rounded-lg p-4">
              <div className="text-gray-400 text-xs mb-1">경과 시간</div>
              <div className="text-white text-xl">{Math.floor(currentTime)}초</div>
            </div>
          </div>

          {/* 자세 데이터 */}
          <div className="bg-gray-800 rounded-lg p-4 mb-4">
            <h3 className="text-white text-sm mb-3">로켓 자세</h3>
            <div className="grid grid-cols-3 gap-4">
              <div>
                <div className="text-gray-400 text-xs mb-1">Pitch</div>
                <div className="text-blue-400">{currentData.pitch.toFixed(2)}°</div>
              </div>
              <div>
                <div className="text-gray-400 text-xs mb-1">Roll</div>
                <div className="text-green-400">{currentData.roll.toFixed(2)}°</div>
              </div>
              <div>
                <div className="text-gray-400 text-xs mb-1">Yaw</div>
                <div className="text-red-400">{currentData.yaw.toFixed(2)}°</div>
              </div>
            </div>
          </div>

          {/* 궤적 시각화 */}
          <div className="bg-gray-800 rounded-lg p-4">
            <h3 className="text-white text-sm mb-3">비행 궤적</h3>
            <div className="relative h-48 bg-gray-900 rounded">
              <svg className="w-full h-full" viewBox="0 0 400 200" preserveAspectRatio="none">
                {/* 그리드 */}
                <defs>
                  <pattern id="grid" width="20" height="20" patternUnits="userSpaceOnUse">
                    <path d="M 20 0 L 0 0 0 20" fill="none" stroke="#374151" strokeWidth="0.5"/>
                  </pattern>
                </defs>
                <rect width="400" height="200" fill="url(#grid)" />
                
                {/* 궤적 경로 */}
                <path
                  d={launch.telemetryData.slice(0, Math.floor(currentTime) + 1).map((d, i) => {
                    const x = (i / launch.duration) * 400;
                    const y = 200 - (d.altitude / launch.maxAltitude) * 180;
                    return `${i === 0 ? 'M' : 'L'} ${x} ${y}`;
                  }).join(' ')}
                  fill="none"
                  stroke="#3b82f6"
                  strokeWidth="2"
                />
                
                {/* 현재 위치 */}
                <circle
                  cx={(currentTime / launch.duration) * 400}
                  cy={200 - (currentData.altitude / launch.maxAltitude) * 180}
                  r="4"
                  fill="#ef4444"
                />
              </svg>
            </div>
          </div>
        </div>

        {/* 컨트롤 */}
        <div className="p-4 border-t border-gray-800">
          {/* 타임라인 */}
          <div className="mb-4">
            <input
              type="range"
              min="0"
              max={launch.duration}
              value={currentTime}
              onChange={(e) => setCurrentTime(Number(e.target.value))}
              className="w-full h-2 bg-gray-700 rounded-lg appearance-none cursor-pointer slider"
            />
            <div className="flex justify-between text-xs text-gray-400 mt-1">
              <span>0:00</span>
              <span>{Math.floor(currentTime / 60)}:{(Math.floor(currentTime) % 60).toString().padStart(2, '0')}</span>
              <span>{Math.floor(launch.duration / 60)}:{(launch.duration % 60).toString().padStart(2, '0')}</span>
            </div>
          </div>

          {/* 버튼 */}
          <div className="flex items-center justify-center gap-3">
            <button
              onClick={handleReset}
              className="bg-gray-700 hover:bg-gray-600 text-white px-4 py-2 rounded transition-colors flex items-center gap-2"
            >
              <RotateCcw className="w-4 h-4" />
              리셋
            </button>
            <button
              onClick={() => setIsPlaying(!isPlaying)}
              className="bg-blue-600 hover:bg-blue-700 text-white px-6 py-2 rounded transition-colors flex items-center gap-2"
            >
              {isPlaying ? <Pause className="w-5 h-5" /> : <Play className="w-5 h-5" />}
              {isPlaying ? '일시정지' : '재생'}
            </button>
            <div className="flex items-center gap-2 bg-gray-700 rounded px-3 py-2">
              <FastForward className="w-4 h-4 text-white" />
              <select
                value={playbackSpeed}
                onChange={(e) => setPlaybackSpeed(Number(e.target.value))}
                className="bg-transparent text-white text-sm outline-none"
              >
                <option value="0.5">0.5x</option>
                <option value="1">1x</option>
                <option value="2">2x</option>
                <option value="5">5x</option>
                <option value="10">10x</option>
              </select>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
