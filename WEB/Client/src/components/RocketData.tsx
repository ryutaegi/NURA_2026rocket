import { RocketTelemetry } from './MainPage';
import { Gauge, Thermometer, Wind, Battery } from 'lucide-react';

interface RocketDataProps {
  telemetry: RocketTelemetry;
}

export default function RocketData({ telemetry }: RocketDataProps) {
  const dataItems = [
    {
      label: '고도',
      value: `${telemetry.altitude.toFixed(0)} m`,
      icon: Gauge,
      color: 'text-blue-400',
    },
    {
      label: '기압',
      value: `${telemetry.pressure.toFixed(1)} hPa`,
      icon: Gauge,
      color: 'text-purple-400',
    },
  ];

  return (
    <div className="h-full flex flex-col">
      <h3 className="text-white mb-4">텔레메트리 데이터</h3>
      
      <div className="space-y-4 flex-1 overflow-y-auto hide-scrollbar">
        {dataItems.map((item, index) => {
          const Icon = item.icon;
          return (
            <div key={index} className="bg-gray-800 rounded-lg p-4">
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <Icon className={`w-5 h-5 ${item.color}`} />
                  <span className="text-sm text-gray-400">{item.label}</span>
                </div>
              </div>
              <div className={`text-2xl ${item.color}`}>
                {item.value}
              </div>
              
              {/* 배터리 바 */}
              {item.label === '배터리' && (
                <div className="mt-2 w-full bg-gray-700 rounded-full h-2">
                  <div
                    className={`h-2 rounded-full transition-all ${
                      telemetry.battery > 20 ? 'bg-green-500' : 'bg-red-500'
                    }`}
                    style={{ width: `${telemetry.battery}%` }}
                  />
                </div>
              )}
            </div>
          );
        })}
      </div>

      {/* 좌표 정보 */}
      <div className="mt-4 bg-gray-800 rounded-lg p-4">
        <div className="text-sm text-gray-400 mb-2">GPS 좌표</div>
        <div className="text-xs text-white font-mono">
          <div>위도: {telemetry.latitude.toFixed(6)}°</div>
          <div>경도: {telemetry.longitude.toFixed(6)}°</div>
        </div>
      </div>
    </div>
  );
}
