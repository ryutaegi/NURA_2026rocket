import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

def plot_all_columns(csv_file):
    csv_file = Path(csv_file)
    df = pd.read_csv(csv_file)

    # 시간축 생성 (ms → sec)
    if "timeMs" not in df.columns:
        raise ValueError("CSV must contain 'timeMs' column")

    df["timeSec"] = (df["timeMs"] - df["timeMs"].iloc[0]) / 1000.0

    # timeMs 제외한 모든 컬럼 자동 그래프 생성
    for column in df.columns:
        if column in ["timeMs", "timeSec"]:
            continue

        # 숫자형 컬럼만 처리
        if not pd.api.types.is_numeric_dtype(df[column]):
            continue

        plt.figure()
        plt.plot(df["timeSec"], df[column])
        plt.xlabel("Time (s)")
        plt.ylabel(column)
        plt.title(column)
        plt.grid()

        output_path = csv_file.with_name(f"{csv_file.stem}_{column}.png")
        plt.savefig(output_path, dpi=300)
        plt.close()

        print(f"Saved: {output_path}")