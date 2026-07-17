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

  const progress = duration > 0 ? (currentTime / duration) * 100 : 0;

  return (
    <div className="space-y-4">
      <h3 className="text-[#222222] font-semibold text-sm">리플레이 컨트롤</h3>

      {/* 타임라인 */}
      <div>
        <input
          type="range"
          min="0"
          max={duration}
          step="0.1"
          value={currentTime}
          onChange={(e) => onTimeChange(Number(e.target.value))}
          className="w-full h-2 rounded-lg appearance-none cursor-pointer"
          style={{
            background: `linear-gradient(to right, #ff385c 0%, #ff385c ${progress}%, #ebebeb ${progress}%, #ebebeb 100%)`,
          }}
        />
        <div className="flex justify-between text-xs text-[#6a6a6a] mt-1.5">
          <span>{formatTime(currentTime)}</span>
          <span>{formatTime(duration)}</span>
        </div>
      </div>

      {/* 컨트롤 버튼 */}
      <div className="flex items-center gap-2">
        <button
          onClick={onReset}
          className="bg-[#f2f2f2] hover:bg-[#ebebeb] text-[#222222] p-2 rounded-lg transition-colors border border-[#dddddd]"
          title="리셋"
        >
          <RotateCcw className="w-4 h-4" />
        </button>

        <button
          onClick={onPlayPause}
          className="flex-1 bg-[#ff385c] hover:bg-[#e00b41] text-white px-4 py-2 rounded-lg transition-colors flex items-center justify-center gap-2 font-medium text-sm"
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

        <div className="flex items-center gap-2 bg-[#f2f2f2] rounded-lg px-3 py-2 border border-[#dddddd]">
          <FastForward className="w-4 h-4 text-[#6a6a6a]" />
          <select
            value={speed}
            onChange={(e) => onSpeedChange(Number(e.target.value))}
            className="bg-transparent text-[#222222] text-sm outline-none cursor-pointer"
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
      <div className="bg-[#f7f7f7] rounded-xl p-3 border border-[#ebebeb]">
        <div className="flex items-center justify-between text-xs text-[#6a6a6a] mb-1.5">
          <span>재생 진행률</span>
          <span className="text-[#ff385c] font-medium">{progress.toFixed(1)}%</span>
        </div>
        <div className="w-full bg-[#ebebeb] rounded-full h-1.5">
          <div
            className="bg-[#ff385c] h-1.5 rounded-full transition-all"
            style={{ width: `${progress}%` }}
          />
        </div>
      </div>
    </div>
  );
}
