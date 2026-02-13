# parse_flight_bin.py
import struct
import csv
import sys
from pathlib import Path

# FlightState 매핑(원하면 CSV에 문자열로도 저장 가능)
FLIGHT_STATE = ["STANDBY", "LAUNCHED", "POWERED", "COASTING", "APOGEE", "DESCENT", "LANDED"]

# FlightData 레이아웃 (packed, little-endian)
# imu: 6f
# baro: 4f
# gps: 2i 3f sats(B) fix(B)
# angles/servo: 5f
# times: 4I
# state: B
# timeMs: I
FMT = "<6f4f2i3fBB5f4IBI"
REC_SIZE = struct.calcsize(FMT)  # 103이어야 함

COLUMNS = [
    # imu
    "imu_ax", "imu_ay", "imu_az", "imu_gx", "imu_gy", "imu_gz",
    # baro
    "baro_pressure_hPa", "baro_temperature_C", "baro_altitude_m", "baro_climbRate_mps",
    # gps
    "gps_latE7", "gps_lonE7", "gps_altitude_m", "gps_speed_mps", "gps_heading_deg", "gps_sats", "gps_fix",
    # angles/servo
    "roll_deg", "filterRoll_deg", "pitch_deg", "yaw_deg", "servoDegree_deg",
    # times
    "baroTimeMs", "gpsTimeMs", "aTimeMs", "aRxTimeMs",
    # state & time
    "state", "timeMs",
    # (추가) state string
    "stateStr",
]

def read_header_if_any(f):
    """
    헤더가 있으면 (has_header=True, version, rec_size, data_offset)을 반환.
    없으면 (False, None, REC_SIZE, 0)
    """
    start = f.read(8)
    if len(start) < 8:
        return (False, None, REC_SIZE, 0)

    magic = start[:4]
    if magic == b"RLG1":
        version, rec_size = struct.unpack("<HH", start[4:8])
        return (True, version, rec_size, 8)

    # 헤더가 아니면 파일 포인터를 처음으로 되돌림
    f.seek(0)
    return (False, None, REC_SIZE, 0)

def parse_bin_to_csv(bin_path: Path, csv_path: Path):
    with bin_path.open("rb") as f:
        has_hdr, version, rec_size, offset = read_header_if_any(f)

        if rec_size != REC_SIZE:
            print(f"[WARN] rec_size mismatch. file rec_size={rec_size}, expected={REC_SIZE}")
            print("       구조체가 바뀌었거나, 다른 포맷의 로그일 수 있어요.")
            # 그래도 file rec_size로 읽어보긴 어려움(언팩 포맷은 고정이라)
            # 여기서는 안전하게 종료
            return 2

        f.seek(offset)

        with csv_path.open("w", newline="", encoding="utf-8") as out:
            w = csv.writer(out)
            w.writerow(COLUMNS)

            n = 0
            while True:
                chunk = f.read(REC_SIZE)
                if not chunk:
                    break
                if len(chunk) != REC_SIZE:
                    print(f"[WARN] 마지막 레코드가 잘렸습니다. len={len(chunk)} (무시)")
                    break

                vals = struct.unpack(FMT, chunk)

                # gps_fix(B) -> bool
                vals = list(vals)
                gps_fix = bool(vals[16])  # gps_fix 위치(0-based) 계산 결과: 16
                vals[16] = int(gps_fix)

                state = vals[-2]  # state는 끝에서 두 번째
                state_str = FLIGHT_STATE[state] if 0 <= state < len(FLIGHT_STATE) else "UNKNOWN"

                row = vals + [state_str]
                w.writerow(row)
                n += 1

    print(f"OK: {bin_path.name} -> {csv_path.name}  (records={n}, header={has_hdr}, version={version})")
    return 0

def main():
    if len(sys.argv) < 2:
        print("사용법:")
        print("  python parse_flight_bin.py FL0001.BIN")
        print("  python parse_flight_bin.py FL0001.BIN output.csv")
        return 1

    bin_path = Path(sys.argv[1])
    if not bin_path.exists():
        print("파일이 없어요:", bin_path)
        return 1

    if len(sys.argv) >= 3:
        csv_path = Path(sys.argv[2])
    else:
        csv_path = bin_path.with_suffix(".csv")

    return parse_bin_to_csv(bin_path, csv_path)

if __name__ == "__main__":
    raise SystemExit(main())
