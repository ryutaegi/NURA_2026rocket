# parse2.py
import csv
import struct
import sys
from pathlib import Path

FLIGHT_STATE = [
    "STANDBY",
    "POWERED",
    "COASTING",
    "APOGEE",
    "DESCENT",
    "LANDED",
]

# -----------------------------------------------------------------------------
# Packed FlightData layouts, little-endian
#
# 103-byte old layout:
#   imu(6f) + baro(4f) + gps(2i, 3f, B, B)
#   + roll/filterRoll/pitch/yaw/servoDegree(5f)
#   + times(4I) + state(B) + timeMs(I)
#
# 111-byte current layout:
#   old layout + veltotal(float) + velimu(float)
#
# IMPORTANT ASSUMPTION:
#   In FlightData, veltotal and velimu are placed immediately after
#   servoDegree and before baroTimeMs.
# -----------------------------------------------------------------------------
FMT_103 = "<6f4f2i3fBB5f4IBI"
FMT_111 = "<6f4f2i3fBB7f4IBI"

REC_SIZE_103 = struct.calcsize(FMT_103)
REC_SIZE_111 = struct.calcsize(FMT_111)

COLUMNS_103 = [
    "imu_ax", "imu_ay", "imu_az", "imu_gx", "imu_gy", "imu_gz",
    "baro_pressure_hPa", "baro_temperature_C", "baro_altitude_m", "baro_climbRate_mps",
    "gps_latE7", "gps_lonE7", "gps_altitude_m", "gps_speed_mps", "gps_heading_deg",
    "gps_sats", "gps_fix",
    "roll_deg", "filterRoll_deg", "pitch_deg", "yaw_deg", "servoDegree_deg",
    "baroTimeMs", "gpsTimeMs", "aTimeMs", "aRxTimeMs",
    "state", "timeMs",
    "stateStr",
]

COLUMNS_111 = [
    "imu_ax", "imu_ay", "imu_az", "imu_gx", "imu_gy", "imu_gz",
    "baro_pressure_hPa", "baro_temperature_C", "baro_altitude_m", "baro_climbRate_mps",
    "gps_latE7", "gps_lonE7", "gps_altitude_m", "gps_speed_mps", "gps_heading_deg",
    "gps_sats", "gps_fix",
    "roll_deg", "filterRoll_deg", "pitch_deg", "yaw_deg", "servoDegree_deg",
    "veltotal_cmps", "velimu_cmps",
    "baroTimeMs", "gpsTimeMs", "aTimeMs", "aRxTimeMs",
    "state", "timeMs",
    "stateStr",
]

LAYOUTS = {
    REC_SIZE_103: (FMT_103, COLUMNS_103, "old-103"),
    REC_SIZE_111: (FMT_111, COLUMNS_111, "current-111"),
}


def read_header_if_any(f):
    """Return (has_header, version, rec_size, data_offset)."""
    start = f.read(8)

    if len(start) < 8:
        f.seek(0)
        return False, None, None, 0

    if start[:4] == b"RLG1":
        version, rec_size = struct.unpack("<HH", start[4:8])
        return True, version, rec_size, 8

    f.seek(0)
    return False, None, None, 0


def detect_headerless_record_size(file_size: int):
    """Best-effort detection for files without the 8-byte RLG1 header."""
    candidates = [size for size in LAYOUTS if file_size % size == 0]

    if len(candidates) == 1:
        return candidates[0]

    if len(candidates) > 1:
        # Prefer the current format when both happen to divide evenly.
        return REC_SIZE_111

    return None


def choose_layout(rec_size: int):
    layout = LAYOUTS.get(rec_size)
    if layout is None:
        supported = ", ".join(str(size) for size in sorted(LAYOUTS))
        raise ValueError(
            f"Unsupported record size: {rec_size} bytes. "
            f"Supported sizes: {supported} bytes."
        )
    return layout


def parse_bin_to_csv(bin_path: Path, csv_path: Path):
    file_size = bin_path.stat().st_size

    with bin_path.open("rb") as f:
        has_header, version, header_rec_size, offset = read_header_if_any(f)

        if has_header:
            rec_size = header_rec_size
        else:
            rec_size = detect_headerless_record_size(file_size)
            if rec_size is None:
                print("ERROR: Could not detect the record size of a headerless BIN file.")
                print(f"       File size: {file_size} bytes")
                print(f"       Expected a multiple of {REC_SIZE_103} or {REC_SIZE_111} bytes.")
                return 2

        try:
            fmt, columns, layout_name = choose_layout(rec_size)
        except ValueError as exc:
            print(f"ERROR: {exc}")
            print("       The exact FlightData definition from flightType.h is required.")
            return 2

        payload_size = file_size - offset
        full_records, remainder = divmod(payload_size, rec_size)

        print(f"Header       : {has_header}")
        print(f"Version      : {version}")
        print(f"Record size  : {rec_size} bytes")
        print(f"Layout       : {layout_name}")
        print(f"Full records : {full_records}")

        if remainder:
            print(f"WARN: {remainder} trailing byte(s) do not form a complete record and will be ignored.")

        f.seek(offset)

        with csv_path.open("w", newline="", encoding="utf-8-sig") as out:
            writer = csv.writer(out)
            writer.writerow(columns)

            record_count = 0

            while True:
                chunk = f.read(rec_size)

                if not chunk:
                    break

                if len(chunk) != rec_size:
                    print(
                        f"WARN: Last record is truncated: "
                        f"got {len(chunk)} byte(s), expected {rec_size}. Ignored."
                    )
                    break

                values = list(struct.unpack(fmt, chunk))

                # gps_fix is always index 16 in both layouts.
                values[16] = 1 if values[16] else 0

                # Both layouts end with: state(B), timeMs(I)
                state = values[-2]
                if 0 <= state < len(FLIGHT_STATE):
                    state_str = FLIGHT_STATE[state]
                else:
                    state_str = "UNKNOWN"

                writer.writerow(values + [state_str])
                record_count += 1

    print(
        f"OK: {bin_path.name} -> {csv_path.name} "
        f"(records={record_count}, rec_size={rec_size}, version={version})"
    )
    return 0


def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python parse2.py FL0001.BIN")
        print("  python parse2.py FL0001.BIN output.csv")
        return 1

    bin_path = Path(sys.argv[1])

    if not bin_path.is_file():
        print(f"ERROR: File not found: {bin_path}")
        return 1

    csv_path = Path(sys.argv[2]) if len(sys.argv) >= 3 else bin_path.with_suffix(".csv")

    try:
        return parse_bin_to_csv(bin_path, csv_path)
    except (OSError, struct.error) as exc:
        print(f"ERROR: {exc}")
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
