import struct
import pandas as pd
import matplotlib.pyplot as plt

# ===============================
# FlightData 구조체 정의
# ===============================
# ImuData: float ax, ay, az, gx, gy, gz -> 6 floats
# BaroData: float pressure, temperature, altitude, climbRate -> 4 floats
# GpsData: int32 latitudeE7, longitudeE7; float altitude, speed, heading; uint8 sats; bool fix -> 2i 3f B ?
# FlightData 나머지: float roll, filterRoll, pitch, yaw, servoDegree -> 5 floats
# uint32_t baroTimeMs, gpsTimeMs, aTimeMs, aRxTimeMs -> 4I
# FlightState state -> B
# uint32_t timeMs -> I
# ===============================

flight_struct_fmt = '<6f4f2i3fBB5f4IBI'
record_size = struct.calcsize(flight_struct_fmt)
print(f'Each record size = {record_size} bytes')

# ===============================
# 파일 열고 FlightData 읽기
# ===============================
filename = 'FL0016.BIN'
records = []

with open(filename, 'rb') as f:
    while True:
        chunk = f.read(record_size)
        if not chunk or len(chunk) != record_size:
            break
        data = struct.unpack(flight_struct_fmt, chunk)

        record = {
            'ax': data[0], 'ay': data[1], 'az': data[2],
            'gx': data[3], 'gy': data[4], 'gz': data[5],
            'pressure': data[6], 'temperature': data[7],
            'altitude': data[8], 'climbRate': data[9],
            'latitudeE7': data[10], 'longitudeE7': data[11],
            'gpsAltitude': data[12], 'speed': data[13], 'heading': data[14],
            'sats': data[15], 'fix': bool(data[16]),
            'roll': data[17], 'filterRoll': data[18],
            'pitch': data[19], 'yaw': data[20],
            'servoDegree': data[21],
            'baroTimeMs': data[22], 'gpsTimeMs': data[23],
            'aTimeMs': data[24], 'aRxTimeMs': data[25],
            'state': data[26], 'timeMs': data[27]
        }
        records.append(record)

# ===============================
# DataFrame 생성
# ===============================
df = pd.DataFrame(records)
print(f'Total records: {len(df)}')
print(df.head())

# ===============================
# CSV 저장
# ===============================
csv_filename = 'flight_000.csv'
df.to_csv(csv_filename, index=False)
print(f'CSV saved as {csv_filename}')

# ===============================
# 그래프
# ===============================
plt.figure(figsize=(12, 8))

# 1) 고도 & climbRate
plt.subplot(2, 1, 1)
plt.plot(df['timeMs']/1000, df['altitude'], label='Baro Altitude (m)')
plt.plot(df['timeMs']/1000, df['climbRate'], label='Climb Rate (m/s)')
plt.xlabel('Time [s]')
plt.ylabel('Altitude / ClimbRate')
plt.legend()
plt.grid(True)

# 2) IMU roll, pitch, yaw
plt.subplot(2, 1, 2)
plt.plot(df['timeMs']/1000, df['roll'], label='Roll (deg)')
plt.plot(df['timeMs']/1000, df['pitch'], label='Pitch (deg)')
plt.plot(df['timeMs']/1000, df['yaw'], label='Yaw (deg)')
plt.xlabel('Time [s]')
plt.ylabel('Angles [deg]')
plt.legend()
plt.grid(True)

plt.tight_layout()
plt.show()
