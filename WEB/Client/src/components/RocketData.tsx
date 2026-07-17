import { RocketTelemetry } from './MainPage';
import { Gauge, Thermometer, MapPin, Cloud, Pin } from 'lucide-react';
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
      color: 'text-[#ff385c]',
    },
    {
      label: '온도',
      value: `${telemetry.temperature.toFixed(1)} °C`,
      icon: Thermometer,
      color: 'text-orange-500',
    },
    {
      label: '커넥트핀',
      value: telemetry.connectPin ? '해제됨' : '연결됨',
      icon: Pin,
      color: telemetry.connectPin ? 'text-[#ff385c]' : 'text-green-600',
    },
    {
      label: '낙하산',
      value: telemetry.parachute ? '사출됨' : '미사출',
      icon: Cloud,
      color: telemetry.parachute ? 'text-green-600' : 'text-[#929292]',
      subValue: telemetry.parachute
        ? (['알 수 없음', '비상사출', '고도하강', '시간지연'][telemetry.parachuteEjectReason] ?? '알 수 없음')
        : null,
    },
  ];

  return (
    <div className="h-full flex flex-col">
      <h3 className="text-[#222222] font-semibold text-base mb-4">텔레메트리 데이터</h3>

      <div className="space-y-3 flex-1 overflow-y-auto hide-scrollbar">
        {dataItems.map((item, index) => {
          const Icon = item.icon;
          return (
            <div key={index} className="bg-[#f7f7f7] rounded-xl p-3.5 border border-[#ebebeb]">
              <div className="flex items-center gap-2 mb-1.5">
                <Icon className={`w-4 h-4 ${item.color}`} />
                <span className="text-xs text-[#6a6a6a] font-medium">{item.label}</span>
              </div>
              <div className={`text-xl font-semibold ${item.color}`}>
                {item.value}
              </div>
              {'subValue' in item && item.subValue && (
                <div className="text-xs text-[#6a6a6a] mt-1">{item.subValue}</div>
              )}
            </div>
          );
        })}
      </div>

      {/* Roll 실시간 그래프 */}
      <div className="mt-3 bg-[#f7f7f7] rounded-xl p-3.5 border border-[#ebebeb]">
        <div className="flex items-center justify-between mb-2">
          <span className="text-xs text-[#6a6a6a] font-medium">Roll (도)</span>
          <span className="text-sm font-mono text-[#ff385c] font-semibold">{telemetry.roll.toFixed(1)}°</span>
        </div>
        <ResponsiveContainer width="100%" height={140}>
          <LineChart data={rollHistory} margin={{ top: 4, right: 8, left: -10, bottom: 4 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="#ebebeb" />
            <XAxis dataKey="t" hide />
            <YAxis domain={[-127, 128]} ticks={[-90, -45, 0, 45, 90]} tick={{ fontSize: 10, fill: '#929292' }} />
            <ReferenceLine y={0} stroke="#c1c1c1" strokeDasharray="4 2" />
            <Tooltip
              contentStyle={{
                backgroundColor: '#ffffff',
                border: '1px solid #dddddd',
                borderRadius: '8px',
                fontSize: '12px',
                color: '#222222',
                boxShadow: 'rgba(0,0,0,0.1) 0 4px 8px 0'
              }}
              labelFormatter={() => ''}
              formatter={(val: number) => [`${val.toFixed(1)}°`, 'Roll']}
            />
            <Line
              type="monotone"
              dataKey="roll"
              stroke="#ff385c"
              strokeWidth={1.5}
              dot={false}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>

      {/* 좌표 정보 */}
      <div className="mt-3 bg-[#f7f7f7] rounded-xl p-3.5 border border-[#ebebeb]">
        <div className="flex items-center gap-2 mb-2.5">
          <MapPin className="w-4 h-4 text-[#ff385c]" />
          <span className="text-xs text-[#6a6a6a] font-medium">GPS 좌표</span>
        </div>
        <div className="space-y-1.5">
          <div className="flex items-center justify-between">
            <span className="text-xs text-[#6a6a6a]">위도</span>
            <span className="font-mono text-xs text-[#222222] font-medium">{telemetry.latitude.toFixed(6)}°</span>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-xs text-[#6a6a6a]">경도</span>
            <span className="font-mono text-xs text-[#222222] font-medium">{telemetry.longitude.toFixed(6)}°</span>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-xs text-[#6a6a6a]">위성 수</span>
            <span className="font-mono text-xs text-[#222222] font-medium">{telemetry.sats}개</span>
          </div>
        </div>
      </div>
    </div>
  );
}
