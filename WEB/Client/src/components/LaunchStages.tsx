import { Check } from 'lucide-react';

interface LaunchStagesProps {
  stage: 'pre-launch' | 'launch' | 'powered' | 'coasting' | 'apogee' | 'descent' | 'landed';
}

export default function LaunchStages({ stage }: LaunchStagesProps) {
  const stages = [
    { id: 'pre-launch', label: '발사 준비', color: 'gray' },
    { id: 'launch', label: '이륙', color: 'yellow' },
    { id: 'powered', label: '동력 비행', color: 'orange' },
    { id: 'coasting', label: '관성 비행', color: 'rausch' },
    { id: 'apogee', label: '최고 고도', color: 'purple' },
    { id: 'descent', label: '하강', color: 'blue' },
    { id: 'landed', label: '착륙', color: 'green' },
  ];

  const currentIndex = stages.findIndex(s => s.id === stage);

  const getStageColor = (stageId: string, color: string) => {
    const stageIndex = stages.findIndex(s => s.id === stageId);

    if (stageIndex < currentIndex) {
      return 'bg-green-500';
    } else if (stageIndex === currentIndex) {
      const colorMap: Record<string, string> = {
        gray: 'bg-[#dddddd]',
        yellow: 'bg-yellow-400',
        orange: 'bg-orange-500',
        rausch: 'bg-[#ff385c]',
        purple: 'bg-purple-500',
        blue: 'bg-blue-500',
        green: 'bg-green-500',
      };
      return colorMap[color] || 'bg-[#dddddd]';
    }
    return 'bg-[#ebebeb]';
  };

  const getLineColor = (index: number) => {
    if (index < currentIndex) return 'bg-green-500';
    return 'bg-[#ebebeb]';
  };

  const getLabelColor = (index: number) => {
    if (index < currentIndex) return 'text-green-600';
    if (index === currentIndex) return 'text-[#222222]';
    return 'text-[#929292]';
  };

  return (
    <div>
      <h3 className="text-[#222222] font-semibold text-base mb-4">발사 단계</h3>
      <div className="space-y-2">
        {stages.map((s, index) => (
          <div key={s.id} className="flex items-center">
            <div className="flex flex-col items-center mr-4">
              <div
                className={`w-9 h-9 rounded-full ${getStageColor(s.id, s.color)} flex items-center justify-center transition-all ${
                  index === currentIndex ? 'ring-4 ring-[#ff385c]/20 animate-pulse' : ''
                }`}
              >
                {index < currentIndex ? (
                  <Check className="w-4 h-4 text-white" />
                ) : (
                  <span className={`text-sm font-semibold ${index <= currentIndex ? 'text-white' : 'text-[#929292]'}`}>
                    {index + 1}
                  </span>
                )}
              </div>
              {index < stages.length - 1 && (
                <div className={`w-0.5 h-6 ${getLineColor(index)} transition-all mt-1`} />
              )}
            </div>
            <div className="flex-1 min-h-[2.25rem]">
              <div className={`text-sm font-medium ${getLabelColor(index)}`}>
                {s.label}
              </div>
              {index === currentIndex && (
                <div className="text-xs text-[#ff385c] mt-0.5 font-medium">진행 중...</div>
              )}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
