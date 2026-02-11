import { RocketTelemetry } from './MainPage';
import { Gauge, Thermometer, Wind, Battery, MapPin, Droplet, Cloud, Contact2Icon, Pin } from 'lucide-react'; // Droplet, Cloud 아이콘 추가
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
    {
      label: '진행방향 속도',
      value: `${telemetry.speed.toFixed(1)} m/s`,
      icon: Wind,
      color: 'text-green-400',
    },
    {
      label: '온도',
      value: `${telemetry.temperature.toFixed(1)} °C`,
      icon: Thermometer,
      color: 'text-orange-400',
    },
    {
      label: '커넥트핀',
      value: telemetry.connect === 1 ? '해제됨' : '연결됨',
      icon: Pin, // Droplet 아이콘 사용
      color: telemetry.connect === 1 ? 'text-green-400' : 'text-blue-400',
    },
    {
      label: '낙하산',
      value: telemetry.parachuteStatus === 1 ? '전개됨' : '미전개',
      icon: Cloud, // Cloud 아이콘 사용
      color: telemetry.parachuteStatus === 1 ? 'text-green-400' : 'text-blue-400',
    },
    // {
    //   label: '배터리',
    //   value: `${telemetry.battery.toFixed(0)} %`,
    //   icon: Battery,
    //   color: telemetry.battery > 20 ? 'text-green-400' : 'text-red-400',
    // },
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
              
              {/* 배터리 바 (배터리 항목일 때만 렌더링)
              {item.label === '배터리' && (
                <div className="mt-2 w-full bg-gray-700 rounded-full h-2">
                  <div
                    className={`h-2 rounded-full transition-all ${
                      telemetry.battery > 20 ? 'bg-green-500' : 'bg-red-500'
                    }`}
                    style={{ width: `${telemetry.battery}%` }}
                  />
                </div>
              )} */}
            </div>
          );
        })}
      </div>

      {/* 좌표 정보 */}
      <div className="mt-4 bg-gray-800 rounded-lg p-4">
        <div className="flex items-center gap-2 mb-2">
          <MapPin className="w-5 h-5 text-red-400" />
          <span className="text-sm text-gray-400">GPS 좌표</span>
        </div>

        <div className="text-sm text-white space-y-1">
          <div className="flex items-center justify-between">
            <span className="text-gray-400">위도</span>
            <span className="font-mono text-red-400">{telemetry.latitude.toFixed(6)}°</span>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-gray-400">경도</span>
            <span className="font-mono text-red-400">{telemetry.longitude.toFixed(6)}°</span>
          </div>
        </div>
      </div>
      </div>
  );
}

