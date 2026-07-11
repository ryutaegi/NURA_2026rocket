import struct
import csv
from pathlib import Path

INPUT_FILE = "FL0060.BIN"
OUTPUT_FILE = "FL0060.csv"

HEADER_SIZE = 8
EXPECTED_RECORD_SIZE = 111


def state_name(value):
    states = {
        0: "STANDBY",
        1: "POWERED",
        2: "COASTING",
        3: "APOGEE",
        4: "DESCENT",
        5: "LANDED",
    }
    return states.get(value, f"UNKNOWN_{value}")


def parse_record(data: bytes):
    """
    FlightData packed 구조체 111 bytes 해석
    Arduino Mega / AVR:
    float = 4 bytes, uint32_t = 4 bytes, bool = 1 byte
    little-endian
    """

    fmt = (
        "<"          # little-endian
        "6f"         # imu: ax ay az gx gy gz
        "4f"         # baro: pressure temperature altitude climbRate
        "ii"         # gps: latitudeE7 longitudeE7
        "3f"         # gps: altitude speed heading
        "BB"         # gps: sats fix
        "7f"         # roll, filterRoll, pitch, yaw, veltotal, velimu, servoDegree
        "4I"         # baroTimeMs, gpsTimeMs, aTimeMs, aRxTimeMs
        "B"          # FlightState
        "I"          # timeMs
    )

    values = struct.unpack(fmt, data)

    (
        ax, ay, az, gx, gy, gz,
        pressure, temperature, baro_altitude, climb_rate,
        latitude_e7, longitude_e7,
        gps_altitude, gps_speed, gps_heading,
        sats, gps_fix,
        roll, filter_roll, pitch, yaw,
        vel_total, vel_imu, servo_degree,
        baro_time_ms, gps_time_ms, a_time_ms, a_rx_time_ms,
        state, time_ms
    ) = values

    return {
        "time_ms": time_ms,
        "time_s": time_ms / 1000.0,

        "state": state,
        "state_name": state_name(state),

        "imu_ax_mps2": ax,
        "imu_ay_mps2": ay,
        "imu_az_mps2": az,
        "imu_gx_dps": gx,
        "imu_gy_dps": gy,
        "imu_gz_dps": gz,

        "baro_pressure_hpa": pressure,
        "baro_temperature_c": temperature,
        "baro_altitude_m": baro_altitude,
        "baro_climb_rate_mps": climb_rate,

        "gps_latitude": latitude_e7 / 1e7,
        "gps_longitude": longitude_e7 / 1e7,
        "gps_altitude_m": gps_altitude,
        "gps_speed_mps": gps_speed,
        "gps_heading_deg": gps_heading,
        "gps_sats": sats,
        "gps_fix": gps_fix,

        "roll": roll,
        "filter_roll": filter_roll,
        "pitch": pitch,
        "yaw": yaw,

        "veltotal_mps": vel_total,
        "velimu_mps": vel_imu,
        "servo_degree": servo_degree,

        "baro_time_ms": baro_time_ms,
        "gps_time_ms": gps_time_ms,
        "a_time_ms": a_time_ms,
        "a_rx_time_ms": a_rx_time_ms,
    }


def convert_bin_to_csv(input_file, output_file):
    raw = Path(input_file).read_bytes()

    if len(raw) < HEADER_SIZE:
        raise ValueError("파일 크기가 너무 작습니다.")

    # 헤더: magic[4], version uint16, recSize uint16
    magic, version, rec_size = struct.unpack("<4sHH", raw[:HEADER_SIZE])

    print("=== BIN Header ===")
    print("magic   :", magic.decode(errors="replace"))
    print("version :", version)
    print("recSize :", rec_size)

    if magic != b"RLG1":
        raise ValueError("RLG1 헤더가 아닙니다. 올바른 SD 로그 파일인지 확인하세요.")

    if rec_size != EXPECTED_RECORD_SIZE:
        print(f"경고: 현재 코드 기준 FlightData 크기는 {EXPECTED_RECORD_SIZE} bytes입니다.")
        print(f"파일 헤더에는 {rec_size} bytes로 저장되어 있습니다.")
        print("비행 당시 flightType.h 구조체가 달랐을 수 있습니다.")

    payload = raw[HEADER_SIZE:]
    full_count = len(payload) // rec_size
    remainder = len(payload) % rec_size

    print("파일 크기 :", len(raw), "bytes")
    print("레코드 수 :", full_count)
    print("남은 바이트:", remainder)

    rows = []

    for i in range(full_count):
        start = i * rec_size
        end = start + rec_size
        record = payload[start:end]

        if len(record) != EXPECTED_RECORD_SIZE:
            print(f"{i}번 레코드 크기 불일치: {len(record)} bytes")
            continue

        row = parse_record(record)
        row["record_index"] = i
        rows.append(row)

    if not rows:
        raise ValueError("변환 가능한 레코드가 없습니다.")

    fieldnames = list(rows[0].keys())

    with open(output_file, "w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print()
    print("변환 완료")
    print("출력 파일:", output_file)
    print("변환 레코드:", len(rows))


if __name__ == "__main__":
    convert_bin_to_csv(INPUT_FILE, OUTPUT_FILE)