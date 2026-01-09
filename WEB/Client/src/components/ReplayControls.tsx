import { Play, Pause, RotateCcw, FastForward } from 'lucide-react';

interface ReplayControlsProps {
  currentTime: number;
  duration: number;
  isPlaying: boolean;
  speed: number;
  onTimeChange: (time: number) => void;
  onPlayPause: () => void;
  onSpeedChange: (speed: number) => void;
  onReset: () => void;
}

export default function ReplayControls({
  currentTime,
  duration,
  isPlaying,
  speed,
  onTimeChange,
  onPlayPause,
  onSpeedChange,
  onReset,
}: ReplayControlsProps) {
  const formatTime = (seconds: number) => {
    const mins = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);
    return `${mins}:${secs.toString().padStart(2, '0')}`;
  };

  return (
    <div className="space-y-4">
      <h3 className="text-white text-sm">리플레이 컨트롤</h3>

      {/* 타임라인 */}
      <div>
        <input
          type="range"
          min="0"
          max={duration}
          step="0.1"
          value={currentTime}
          onChange={(e) => onTimeChange(Number(e.target.value))}
          className="w-full h-2 bg-gray-700 rounded-lg appearance-none cursor-pointer"
          style={{
            background: `linear-gradient(to right, #8b5cf6 0%, #8b5cf6 ${(currentTime / duration) * 100}%, #374151 ${(currentTime / duration) * 100}%, #374151 100%)`,
          }}
        />
        <div className="flex justify-between text-xs text-gray-400 mt-2">
          <span>{formatTime(currentTime)}</span>
          <span>{formatTime(duration)}</span>
        </div>
      </div>

      {/* 컨트롤 버튼 */}
      <div className="flex items-center gap-2">
        <button
          onClick={onReset}
          className="bg-gray-700 hover:bg-gray-600 text-white p-2 rounded transition-colors"
          title="리셋"
        >
          <RotateCcw className="w-4 h-4" />
        </button>

        <button
          onClick={onPlayPause}
          className="flex-1 bg-purple-600 hover:bg-purple-700 text-white px-4 py-2 rounded transition-colors flex items-center justify-center gap-2"
        >
          {isPlaying ? (
            <>
              <Pause className="w-4 h-4" />
              <span>일시정지</span>
            </>
          ) : (
            <>
              <Play className="w-4 h-4" />
              <span>재생</span>
            </>
          )}
        </button>

        <div className="flex items-center gap-2 bg-gray-700 rounded px-3 py-2">
          <FastForward className="w-4 h-4 text-white" />
          <select
            value={speed}
            onChange={(e) => onSpeedChange(Number(e.target.value))}
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

      {/* 진행률 표시 */}
      <div className="bg-gray-800 rounded-lg p-3">
        <div className="flex items-center justify-between text-xs text-gray-400 mb-1">
          <span>재생 진행률</span>
          <span>{((currentTime / duration) * 100).toFixed(1)}%</span>
        </div>
        <div className="w-full bg-gray-700 rounded-full h-1.5">
          <div
            className="bg-purple-500 h-1.5 rounded-full transition-all"
            style={{ width: `${(currentTime / duration) * 100}%` }}
          />
        </div>
      </div>
    </div>
  );
}
