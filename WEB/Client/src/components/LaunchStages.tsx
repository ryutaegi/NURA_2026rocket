import { Check } from 'lucide-react';

interface LaunchStagesProps {
  stage: 'pre-launch' | 'launch' | 'powered' | 'coasting' | 'apogee' | 'descent' | 'landed';
}

export default function LaunchStages({ stage }: LaunchStagesProps) {
  const stages = [
    { id: 'pre-launch', label: '발사 준비', color: 'gray' },
    { id: 'launch', label: '이륙', color: 'yellow' },
    { id: 'powered', label: '동력 비행', color: 'orange' },
    { id: 'coasting', label: '관성 비행', color: 'cyan' },
    { id: 'apogee', label: '최고 고도', color: 'purple' },
    { id: 'descent', label: '하강', color: 'blue' },
    { id: 'landed', label: '착륙', color: 'green' },
  ];

  const currentIndex = stages.findIndex(s => s.id === stage);

  const getStageColor = (stageId: string, color: string) => {
    const stageIndex = stages.findIndex(s => s.id === stageId);
    
    if (stageIndex < currentIndex) {
      return 'bg-green-600'; // 완료
    } else if (stageIndex === currentIndex) {
      // 현재 단계
      const colorMap: Record<string, string> = {
        gray: 'bg-gray-600',
        yellow: 'bg-yellow-500',
        orange: 'bg-orange-500',
        cyan: 'bg-cyan-500',
        purple: 'bg-purple-500',
        blue: 'bg-blue-500',
        green: 'bg-green-500',
      };
      return colorMap[color] || 'bg-gray-500';
    }
    return 'bg-gray-700'; // 대기
  };

  const getLineColor = (index: number) => {
    if (index < currentIndex) {
      return 'bg-green-600';
    }
    return 'bg-gray-700';
  };

  return (
    <div>
      <h3 className="text-white mb-4">발사 단계</h3>
      <div className="space-y-3">
        {stages.map((s, index) => (
          <div key={s.id} className="flex items-center">
            <div className="flex flex-col items-center mr-4">
              <div className={`w-10 h-10 rounded-full ${getStageColor(s.id, s.color)} flex items-center justify-center transition-all ${index === currentIndex ? 'ring-4 ring-white/30 animate-pulse' : ''}`}>
                {index < currentIndex ? (
                  <Check className="w-5 h-5 text-white" />
                ) : (
                  <span className="text-white text-sm">{index + 1}</span>
                )}
              </div>
              {index < stages.length - 1 && (
                <div className={`w-0.5 h-8 ${getLineColor(index)} transition-all mt-1`} />
              )}
            </div>
            <div className="flex-1 min-h-[2.5rem]">
              <div className={`text-sm ${index <= currentIndex ? 'text-white' : 'text-gray-500'}`}>
                {s.label}
              </div>
              {index === currentIndex && (
                <div className="text-xs text-cyan-400 mt-0.5">진행 중...</div>
              )}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
