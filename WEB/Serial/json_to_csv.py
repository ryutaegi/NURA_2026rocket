#!/usr/bin/env python3
import json
import csv
import math
from pathlib import Path

def convert_raw_telemetry(data):
    """원본 센서 데이터를 변환된 형식으로"""

    # 쿼터니안 Q15 디코딩
    q1 = data['q1'] / 32767.0
    q2 = data['q2'] / 32767.0
    q3 = data['q3'] / 32767.0
    q0 = math.sqrt(max(0, 1 - q1**2 - q2**2 - q3**2))

    # flag1 파싱
    flag1 = data['flag1']
    connectPin = bool(flag1 & 0x01)
    parachute = bool((flag1 >> 1) & 0x01)
    ejectBtn = bool((flag1 >> 2) & 0x01)
    soundBtn = bool((flag1 >> 3) & 0x01)
    sats = (flag1 >> 4) & 0x0F

    # flag2 파싱
    flag2 = data['flag2']
    flightPhase = (flag2 >> 5) & 0x07  # bits 7-5
    ejectDescent = bool((flag2 >> 3) & 0x01)  # bit 3
    ejectTimer = bool((flag2 >> 2) & 0x01)  # bit 2
    extra1 = False  # 사용 안 함

    # 낙하산 사출 이유
    parachuteEjectReason = 1 if ejectBtn else (2 if ejectDescent else (3 if ejectTimer else 0))

    # roll 스케일링
    roll = data['roll'] - 127

    # 좌표 변환
    latitude = data['lat'] / 1e7
    longitude = data['lon'] / 1e7
    altitude = data['alt']

    # 온도
    temperature = data['temp'] - 20

    return {
        'timestamp': data['timestamp'],
        'latitude': latitude,
        'longitude': longitude,
        'altitude': altitude,
        'q0': q0,
        'q1': q1,
        'q2': q2,
        'q3': q3,
        'roll': roll,
        'temperature': temperature,
        'sats': sats,
        'soundBtn': soundBtn,
        'ejectBtn': ejectBtn,
        'parachute': parachute,
        'connectPin': connectPin,
        'flightPhase': flightPhase,
        'extra1': extra1,
        'ejectDescent': ejectDescent,
        'ejectTimer': ejectTimer,
        'parachuteEjectReason': parachuteEjectReason,
    }

def main():
    json_file = Path(__file__).parent / 'launch_data' / 'launch_1778330729183.json'
    csv_file = json_file.parent / 'launch_1778330729183.csv'

    if not json_file.exists():
        print(f"❌ 파일을 찾을 수 없습니다: {json_file}")
        return

    print(f"📖 파일 읽는 중: {json_file.name}")
    with open(json_file, 'r', encoding='utf-8') as f:
        data = json.load(f)

    telemetry_data = data.get('telemetryData', [])
    print(f"📊 {len(telemetry_data)}개 레코드 발견")

    # 변환
    converted_records = [convert_raw_telemetry(record) for record in telemetry_data]

    # CSV 저장
    fieldnames = [
        'timestamp', 'latitude', 'longitude', 'altitude',
        'q0', 'q1', 'q2', 'q3', 'roll', 'temperature',
        'sats', 'soundBtn', 'ejectBtn', 'parachute', 'connectPin',
        'flightPhase', 'extra1', 'ejectDescent', 'ejectTimer', 'parachuteEjectReason'
    ]

    print(f"💾 CSV 저장 중: {csv_file.name}")
    with open(csv_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(converted_records)

    print(f"✅ 완료! {csv_file}")
    print(f"   - 레코드 수: {len(converted_records)}")
    print(f"   - 필드: {', '.join(fieldnames[:5])} ...")

if __name__ == '__main__':
    main()
