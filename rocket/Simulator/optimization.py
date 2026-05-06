"""
optimization.py
---------------
Bayesian Optimization 기반 PD gain 자동 튜닝

구조:
    Optuna가 (Kp, Kd) 조합 제안
    → Monte Carlo 시뮬레이션 실행
    → 성능 점수 J 계산
    → Optuna가 다음 조합 갱신
    → 반복 → 최적 (Kp, Kd) 수렴

목적함수 J (낮을수록 좋음):
    J = (1 - success_rate/100) * 500   ← 안정성 페널티
      + mean_overshoot * 0.5           ← 초과각 페널티
      + mean_settling  * 10.0          ← 응답속도 페널티
      + mean_error     * 2.0           ← 정밀도 페널티
"""

import numpy as np
import optuna
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))

from rocket_model import RocketModel, DEFAULT_PARAMS
from controller   import PDController
from monte_carlo  import MonteCarlo, print_stats


# ─────────────────────────────────────────
# 탐색 범위 설정
# ─────────────────────────────────────────
SEARCH_SPACE = {
    "Kp_base" : (0.5, 5.0),   # V=20 m/s 기준 Kp
    "Kd_base" : (0.1, 3.0),   # V=20 m/s 기준 Kd
}

MC_RUNS_DURING_OPT = 150
MC_RUNS_FINAL      = 500


def objective(trial: optuna.Trial) -> float:
    Kp_base = trial.suggest_float("Kp_base", *SEARCH_SPACE["Kp_base"])
    Kd_base = trial.suggest_float("Kd_base", *SEARCH_SPACE["Kd_base"])

    mc = MonteCarlo(
        n_runs=MC_RUNS_DURING_OPT,
        Kp_base=Kp_base,
        Kd_base=Kd_base,
        verbose=False
    )
    stats = mc.run(phi0_mean=np.deg2rad(30))
    return _score(stats)


def _score(stats: dict) -> float:
    """성능 점수 (낮을수록 좋음) — 성공률 최우선"""
    sr   = stats["success_rate"]
    mos  = stats["mean_overshoot"]
    mst  = stats["mean_settling"] if stats["mean_settling"] > 0 else 10.0
    merr = stats["mean_error"]

    # 성공률 80% 미만이면 강한 페널티
    sr_penalty = max(0, (80.0 - sr)) * 10.0

    J = (sr_penalty
         + (1.0 - sr / 100.0) * 300.0   # 안정성
         + mos  * 2                    # 초과각
         + mst  * 8.0                    # 응답속도
         + merr * 1.5)                   # 정밀도
    return float(J)


def run_optimization(n_trials: int = 50) -> dict:
    """
    Bayesian Optimization 실행

    Parameters
    ----------
    n_trials : Optuna 탐색 횟수 (많을수록 정확, 느림)
               권장: 빠른 테스트 30, 정밀 최적화 100+

    Returns
    -------
    dict: 최적 gain schedule + 검증 통계
    """
    print(f"\n{'='*50}")
    print(f"  Bayesian Optimization 시작")
    print(f"  탐색 횟수: {n_trials} trials")
    print(f"  MC runs/trial: {MC_RUNS_DURING_OPT}")
    print(f"{'='*50}\n")

    # Optuna 설정 (로그 최소화)
    optuna.logging.set_verbosity(optuna.logging.WARNING)

    study = optuna.create_study(
        direction="minimize",
        sampler=optuna.samplers.TPESampler(seed=42),  # Bayesian (TPE)
    )

    # 진행 콜백
    def callback(study, trial):
        if trial.number % 10 == 0:
            print(f"  Trial {trial.number:3d}/{n_trials}"
                  f"  |  Best J = {study.best_value:.2f}")

    study.optimize(objective, n_trials=n_trials, callbacks=[callback])

    # ── 최적값 추출 ──
    best     = study.best_params
    Kp_best  = best["Kp_base"]
    Kd_best  = best["Kd_base"]

    print(f"\n{'='*50}")
    print(f"  최적화 완료!  Best J = {study.best_value:.2f}")
    print(f"{'='*50}")
    print(f"  Kp_base = {Kp_best:.4f}  (V=20 m/s 기준)")
    print(f"  Kd_base = {Kd_best:.4f}  (V=20 m/s 기준)")

    print(f"\n  최종 검증 중... (MC {MC_RUNS_FINAL}회)")
    mc_final = MonteCarlo(
        n_runs=MC_RUNS_FINAL,
        Kp_base=Kp_best,
        Kd_base=Kd_best,
        verbose=True
    )
    final_stats = mc_final.run(phi0_mean=np.deg2rad(30))

    print("\n  [최종 검증 결과]")
    print_stats(final_stats)

    return {
        "best_params"  : best,
        "Kp_base"      : Kp_best,
        "Kd_base"      : Kd_best,
        "best_J"       : study.best_value,
        "final_stats"  : final_stats,
        "study"        : study,
    }


# ─────────────────────────────────────────
# 단독 실행 테스트
# ─────────────────────────────────────────
def visualize_3d_optimization_surface(study):
    """
    최적화 과정을 3D 표면 그래프로 시각화

    X축: Kp_base, Y축: Kd_base, Z축: Score J
    """
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d import Axes3D
    from scipy.interpolate import griddata

    trials = study.trials
    kp_vals = []
    kd_vals = []
    j_vals  = []

    for t in trials:
        if t.value is not None:
            kp_vals.append(t.params.get("Kp_base"))
            kd_vals.append(t.params.get("Kd_base"))
            j_vals.append(t.value)

    kp_vals = np.array(kp_vals)
    kd_vals = np.array(kd_vals)
    j_vals  = np.array(j_vals)

    # 그리드 생성
    kp_grid = np.linspace(kp_vals.min() - 0.2, kp_vals.max() + 0.2, 50)
    kd_grid = np.linspace(kd_vals.min() - 0.2, kd_vals.max() + 0.2, 50)
    KP, KD = np.meshgrid(kp_grid, kd_grid)

    # Interpolation으로 표면 생성
    J = griddata(
        (kp_vals, kd_vals), j_vals,
        (KP, KD),
        method='cubic'
    )

    # 3D 플롯
    fig = plt.figure(figsize=(14, 5))

    # ── 서브플롯 1: 3D 표면 ──
    ax1 = fig.add_subplot(121, projection='3d')
    surf = ax1.plot_surface(KP, KD, J, cmap='viridis', alpha=0.8, edgecolor='none')

    # Trial 점들 표시
    ax1.scatter(kp_vals, kd_vals, j_vals, c='red', s=20, alpha=0.6, label='Trials')

    # 최적점 표시
    best_idx = np.argmin(j_vals)
    ax1.scatter(
        [kp_vals[best_idx]], [kd_vals[best_idx]], [j_vals[best_idx]],
        c='gold', s=200, marker='*', edgecolors='black', linewidth=1.5,
        label=f'Best (J={j_vals[best_idx]:.2f})', zorder=10
    )

    ax1.set_xlabel("Kp_base", fontsize=10, labelpad=8)
    ax1.set_ylabel("Kd_base", fontsize=10, labelpad=8)
    ax1.set_zlabel("Score J", fontsize=10, labelpad=8)
    ax1.set_title("3D 최적화 표면", fontsize=11, fontweight='bold')
    ax1.view_init(elev=25, azim=45)
    ax1.legend(fontsize=8)

    # ── 서브플롯 2: 등고선도 (Contour) ──
    ax2 = fig.add_subplot(122)
    contour = ax2.contourf(KP, KD, J, levels=20, cmap='viridis', alpha=0.8)
    contour_lines = ax2.contour(KP, KD, J, levels=10, colors='black', alpha=0.3, linewidths=0.5)
    ax2.clabel(contour_lines, inline=True, fontsize=7)

    # Trial 점들
    scatter = ax2.scatter(kp_vals, kd_vals, c=j_vals, cmap='Reds',
                         s=30, alpha=0.6, edgecolors='black', linewidth=0.5)

    # 최적점
    ax2.scatter(
        [kp_vals[best_idx]], [kd_vals[best_idx]],
        c='gold', s=300, marker='*', edgecolors='black', linewidth=2,
        zorder=10
    )

    # 최적점 텍스트
    ax2.annotate(
        f"Optimal\nKp={kp_vals[best_idx]:.3f}\nKd={kd_vals[best_idx]:.3f}",
        xy=(kp_vals[best_idx], kd_vals[best_idx]),
        xytext=(kp_vals[best_idx] + 0.3, kd_vals[best_idx] + 0.3),
        fontsize=9, fontweight='bold',
        bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.7),
        arrowprops=dict(arrowstyle='->', connectionstyle='arc3,rad=0.3', lw=1.5)
    )

    ax2.set_xlabel("Kp_base", fontsize=10)
    ax2.set_ylabel("Kd_base", fontsize=10)
    ax2.set_title("최적화 공간 (등고선도)", fontsize=11, fontweight='bold')
    cbar = plt.colorbar(contour, ax=ax2)
    cbar.set_label("Score J", fontsize=9)

    plt.tight_layout()
    return fig


if __name__ == "__main__":
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d import Axes3D
    from scipy.interpolate import griddata
    from utils import set_korean_font, set_excel_style, XL_BLUE, XL_ORANGE, XL_RED, XL_GREEN, XL_YELLOW
    set_korean_font()
    set_excel_style()

    # ── 최적화 실행 ──
    result = run_optimization(n_trials=40)

    Kp_best = result["Kp_base"]
    Kd_best = result["Kd_base"]
    stats   = result["final_stats"]

    # ── 시각화 1: 최적화 수렴 곡선 ──
    study  = result["study"]
    trials = study.trials
    best_so_far = []
    current_best = float('inf')
    for t in trials:
        if t.value is not None and t.value < current_best:
            current_best = t.value
        best_so_far.append(current_best)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4))

    # 수렴 곡선
    trial_vals = [t.value for t in trials if t.value is not None]
    axes[0].plot(trial_vals, color=XL_BLUE, alpha=0.4,
                 linewidth=1, label="Trial score")
    axes[0].plot(best_so_far, color=XL_ORANGE, linewidth=2,
                 label="Best so far")
    axes[0].set_xlabel("Trial")
    axes[0].set_ylabel("Score J")
    axes[0].set_title("Bayesian Optimization 수렴")
    axes[0].legend(fontsize=8)

    # 최종 MC 결과 분포
    raw = stats["raw"]
    error_arr  = np.array([r["final_error"] for r in raw])
    stable_arr = np.array([r["stable"]      for r in raw])
    c = [XL_GREEN if s else XL_RED for s in stable_arr]
    axes[1].scatter(range(len(raw)), error_arr, c=c, s=6, alpha=0.5)
    axes[1].axhline(5.0, color=XL_YELLOW, linewidth=1,
                    linestyle='--', label='5 deg threshold')
    axes[1].set_xlabel("Run index")
    axes[1].set_ylabel("Final error [deg]")
    axes[1].set_title(f"최종 검증  |  성공률 {stats['success_rate']:.1f}%")
    axes[1].legend(fontsize=8)

    plt.tight_layout()
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "outputs")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "optimization_result.png")
    plt.savefig(out_path, dpi=120, facecolor="white")

    # ── 시각화 2: 3D 최적화 표면 ──
    print("\n  3D 최적화 표면 생성 중...")
    fig_3d = visualize_3d_optimization_surface(study)
    out_path_3d = os.path.join(out_dir, "optimization_3d_surface.png")
    fig_3d.savefig(out_path_3d, dpi=120, facecolor="white", bbox_inches='tight')
    print(f"  저장: {out_path_3d}")

    # ── 최적 gain 출력 ──
    print("\n  [controller.py에 붙여넣을 최적 GAIN 값]")
    print(f"  KP_BASE = {Kp_best:.4f}  # V=20 m/s 기준")
    print(f"  KD_BASE = {Kd_best:.4f}  # V=20 m/s 기준")
    print(f"\n  저장된 그래프:")
    print(f"    optimization_result.png")
    print(f"    optimization_3d_surface.png  ← 3D 표면 & 등고선도")
