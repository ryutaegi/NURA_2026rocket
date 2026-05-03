import { RocketTelemetry } from './MainPage';
import { Gauge, Thermometer, Wind, Battery, MapPin, Droplet, Cloud, Contact2Icon, Pin } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';

interface RocketDataProps {
  telemetry: RocketTelemetry;
  rollHistory: { t: number; roll: number }[];
}

export default function RocketData({ telemetry, rollHistory }: RocketDataProps) {
  const dataItems = [
    {
      label: '고도',
      value: `${telemetry.altitude.toFixed(0)} m`,
      icon: Gauge,
      color: 'text-blue-400',
    },
    // {
    //   label: '기압',
    //   value: `${telemetry.pressure.toFixed(1)} hPa`,
    //   icon: Gauge,
    //   color: 'text-purple-400',
    // },
    // {
    //   label: '진행방향 속도',
    //   value: `${telemetry.speed.toFixed(1)} m/s`,
    //   icon: Wind,
    //   color: 'text-green-400',
    // },
    {
      label: '온도',
      value: `${telemetry.temperature.toFixed(1)} °C`,
      icon: Thermometer,
      color: 'text-orange-400',
    },
    {
      label: '커넥트핀',
      value: telemetry.connectPin ? '해제됨' : '연결됨',
      icon: Pin,
      color: telemetry.connectPin ? 'text-blue-400' : 'text-red-400',
    },
    {
      label: '낙하산',
      value: telemetry.parachute ? '사출됨' : '미사출',
      icon: Cloud,
      color: telemetry.parachute ? 'text-green-400' : 'text-red-400',
      subValue: telemetry.parachute
        ? (['알 수 없음', '비상사출', '고도하강', '시간지연'][telemetry.parachuteEjectReason] ?? '알 수 없음')
        : null,
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
              {'subValue' in item && item.subValue && (
                <div className="text-sm text-gray-400 mt-1">{item.subValue}</div>
              )}
              
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

      {/* Roll 실시간 그래프 */}
      <div className="mt-4 bg-gray-800 rounded-lg p-4">
        <div className="flex items-center justify-between mb-2">
          <span className="text-sm text-gray-400">Roll (도)</span>
          <span className="text-sm font-mono text-blue-400">{telemetry.roll.toFixed(1)}°</span>
        </div>
        <ResponsiveContainer width="100%" height={160}>
          <LineChart data={rollHistory} margin={{ top: 4, right: 8, left: -10, bottom: 4 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
            <XAxis dataKey="t" hide />
            <YAxis domain={[-127, 128]} ticks={[-90, -45, 0, 45, 90]} tick={{ fontSize: 10, fill: '#9ca3af' }} />
            <ReferenceLine y={0} stroke="#6b7280" strokeDasharray="4 2" />
            <Tooltip
              contentStyle={{ backgroundColor: '#1f2937', border: 'none', borderRadius: '6px', fontSize: '12px' }}
              labelFormatter={() => ''}
              formatter={(val: number) => [`${val.toFixed(1)}°`, 'Roll']}
            />
            <Line
              type="monotone"
              dataKey="roll"
              stroke="#60a5fa"
              strokeWidth={1.5}
              dot={false}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
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
          <div className="flex items-center justify-between">
            <span className="text-gray-400">위성 수</span>
            <span className="font-mono text-red-400">{telemetry.sats}개</span>
          </div>
        </div>
      </div>
      </div>
  );
}

