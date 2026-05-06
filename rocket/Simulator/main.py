"""
main.py
-------
전체 파이프라인 실행

Step 1: 물리 모델 검증
Step 2: PD 제어기 기본 동작 확인
Step 3: 시뮬레이터 (속도 프로파일 + Gain Scheduling)
Step 4: Monte Carlo robustness 평가
Step 5: Bayesian Optimization으로 gain 최적화
Step 6: 최적 gain으로 최종 검증 + 결과 저장
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))

from utils import set_korean_font, set_excel_style, XL_BLUE, XL_ORANGE, XL_RED, XL_GREEN, XL_YELLOW, XL_GRAY
set_korean_font()
set_excel_style()   # ← 전역 Excel 스타일 적용

from rocket_model import RocketModel, DEFAULT_PARAMS
from controller   import PDController
from simulator    import Simulator, velocity_profile
from monte_carlo  import MonteCarlo, print_stats
from optimization import run_optimization, visualize_3d_optimization_surface

# ─────────────────────────────────────────
# 설정
# ─────────────────────────────────────────
DT          = 0.01     # 타임스텝 [s]
T_SIM       = 10.0     # 시뮬레이션 시간 [s] (연소 4s + 관성비행 6s)
PHI0_DEG    = 30.0     # 초기 roll 각도 [deg]
MC_RUNS     = 300      # Monte Carlo 횟수
BO_TRIALS   = 100       # Bayesian Optimization trial 수

OUTPUT_DIR  = os.path.dirname(os.path.abspath(__file__))
OUTPUTS_DIR = os.path.join(OUTPUT_DIR, "outputs")
RESULT_PNG  = os.path.join(OUTPUT_DIR, "final_result.png")


def separator(title: str):
    print(f"\n{'='*55}")
    print(f"  {title}")
    print(f"{'='*55}")


# ─────────────────────────────────────────
# Step 1: 물리 모델 검증
# ─────────────────────────────────────────
def step1_model_check():
    separator("Step 1 / 5  —  물리 모델 검증")

    rocket = RocketModel()
    rocket.reset(phi0=np.deg2rad(PHI0_DEG))

    time_arr = np.arange(0, T_SIM, DT)
    phi_log, p_log = [], []

    for _ in time_arr:
        phi, p = rocket.step(delta_cmd=0.0, V=50.0, dt=DT)
        phi_log.append(np.rad2deg(phi))
        p_log.append(np.rad2deg(p))

    print(f"  초기 φ    = {PHI0_DEG:.1f} deg")
    print(f"  최종 φ    = {phi_log[-1]:.3f} deg  (제어 없음 → 일정해야 정상)")
    print(f"  최종 p    = {p_log[-1]:.3f} deg/s")
    ok = abs(phi_log[-1] - PHI0_DEG) < 0.1
    print(f"  검증 결과 : {'✅ PASS' if ok else '❌ FAIL'}")
    return time_arr, phi_log, p_log


# ─────────────────────────────────────────
# Step 2: 기본 PD 제어기 동작
# ─────────────────────────────────────────
def step2_controller_check():
    separator("Step 2 / 5  —  PD 제어기 기본 동작")

    rocket = RocketModel()
    ctrl   = PDController(use_scheduling=False, Kp=0.5, Kd=0.1)
    sim    = Simulator(rocket=rocket, controller=ctrl, dt=DT, T=T_SIM)
    res    = sim.run(phi0=np.deg2rad(PHI0_DEG))

    print(f"  안정화 여부 : {'✅ 안정' if res['stable'] else '❌ 불안정'}")
    print(f"  최대 초과각 : {res['max_overshoot']:.2f} deg")
    print(f"  최종 오차   : {res['final_error']:.3f} deg")
    return res


# ─────────────────────────────────────────
# Step 3: 시뮬레이터 (Gain Scheduling)
# ─────────────────────────────────────────
def step3_simulator_check():
    separator("Step 3 / 5  —  시뮬레이터 (Gain Scheduling)")

    rocket = RocketModel()
    ctrl   = PDController(use_scheduling=True)
    sim    = Simulator(rocket=rocket, controller=ctrl, dt=DT, T=T_SIM)
    res    = sim.run(phi0=np.deg2rad(PHI0_DEG))

    print(f"  안정화 여부 : {'✅ 안정' if res['stable'] else '❌ 불안정'}")
    print(f"  안정화 시간 : {res['settling_time']:.2f} s")
    print(f"  최대 초과각 : {res['max_overshoot']:.2f} deg")
    print(f"  최종 오차   : {res['final_error']:.3f} deg")
    print(f"  성능 점수 J : {sim.score(res):.2f}")
    return res


# ─────────────────────────────────────────
# Step 4: Monte Carlo (최적화 전)
# ─────────────────────────────────────────
def step4_monte_carlo_before():
    separator("Step 4 / 5  —  Monte Carlo (최적화 전)")
    print(f"  {MC_RUNS}회 실행 중...\n")

    mc    = MonteCarlo(n_runs=MC_RUNS, verbose=True)
    stats = mc.run(phi0_mean=np.deg2rad(PHI0_DEG))
    print_stats(stats)
    return stats


# ─────────────────────────────────────────
# Step 5: Bayesian Optimization
# ─────────────────────────────────────────
def step5_optimization():
    separator("Step 5 / 5  —  Bayesian Optimization")
    result = run_optimization(n_trials=BO_TRIALS)
    return result


# ─────────────────────────────────────────
# 다중 초기각도 궤적 저장
# ─────────────────────────────────────────
def save_trajectory_plots(best_kp: float, best_kd: float):
    from simulator import THRUST_TIME, BURN_END

    separator("다중 초기각도 궤적 저장")
    os.makedirs(OUTPUTS_DIR, exist_ok=True)

    PHI0_SAMPLES_DEG = [-90, -70, -50, -30, 20, 40, 60, 80]
    N_TRAJ = len(PHI0_SAMPLES_DEG)

    traj_list = []
    for phi0_deg in PHI0_SAMPLES_DEG:
        r_i   = RocketModel()
        c_i   = PDController(Kp_base=best_kp, Kd_base=best_kd)
        s_i   = Simulator(rocket=r_i, controller=c_i, T=T_SIM)
        res_i = s_i.run(phi0=np.deg2rad(phi0_deg))
        traj_list.append((phi0_deg, res_i))
        status = "안정" if res_i["stable"] else "불안정"
        print(f"  phi0={phi0_deg:+5.0f} deg  ->  {status}"
              f"  err={res_i['final_error']:.2f} deg")

    threshold_deg = np.rad2deg(Simulator().threshold)
    burn_cutoff   = THRUST_TIME[-1] - 3.5

    # ── 그림 1: 4x2 그리드 ──────────────────────────────
    NCOLS = 2
    NROWS = (N_TRAJ + NCOLS - 1) // NCOLS

    fig3, axes3 = plt.subplots(NROWS, NCOLS,
                                figsize=(13, NROWS * 3.2),
                                sharex=True)

    for idx, (phi0_deg, res_i) in enumerate(traj_list):
        row, col = divmod(idx, NCOLS)
        ax = axes3[row][col]

        stable = res_i["stable"]
        c_phi  = XL_BLUE if stable else XL_RED
        c_fin  = XL_GREEN
        t = res_i["time"]

        ax.plot(t, res_i["phi"], color=c_phi, linewidth=1.6,
                label="roll [deg]")
        ax.axhspan(-threshold_deg, threshold_deg,
                   color=XL_GREEN, alpha=0.06)
        ax.axhline( threshold_deg, color=XL_GREEN,
                    linewidth=0.8, linestyle='--')
        ax.axhline(-threshold_deg, color=XL_GREEN,
                    linewidth=0.8, linestyle='--')

        ax2b = ax.twinx()
        ax2b.plot(t, res_i["delta"], color=c_fin,
                  linewidth=1.2, linestyle='--', alpha=0.8,
                  label="fin [deg]")
        ax2b.set_ylabel("Fin [deg]", color=c_fin, fontsize=8)
        ax2b.tick_params(colors=c_fin, labelsize=7)
        ax2b.spines["right"].set_edgecolor(c_fin)
        for sp in ["top", "left", "bottom"]:
            ax2b.spines[sp].set_visible(False)

        ax.axvspan(0, burn_cutoff, color=XL_YELLOW, alpha=0.06)
        ax.axvline(burn_cutoff, color=XL_YELLOW,
                   linewidth=0.8, linestyle=':')

        if res_i["settling_time"] > 0:
            ax.axvline(res_i["settling_time"], color=XL_GRAY,
                       linewidth=0.9, linestyle='--')

        label_stable = "O stable" if stable else "X unstable"
        ax.set_title(
            f"phi0 = {phi0_deg:+.0f} deg   [{label_stable}]"
            f"   err={res_i['final_error']:.1f} deg",
            fontsize=9, pad=4)
        ax.set_ylabel("Roll [deg]", fontsize=8)
        ax.tick_params(labelsize=7)
        if row == NROWS - 1:
            ax.set_xlabel("Time [s]", fontsize=8)

    for idx in range(N_TRAJ, NROWS * NCOLS):
        row, col = divmod(idx, NCOLS)
        axes3[row][col].set_visible(False)

    n_stable = sum(1 for _, r in traj_list if r["stable"])
    fig3.suptitle(
        f"초기각도별 Roll 제어 궤적  |  {n_stable}/{N_TRAJ} 안정화"
        f"  (Kp={best_kp:.3f}, Kd={best_kd:.3f})",
        fontsize=11, y=1.01)
    fig3.tight_layout()
    path3 = os.path.join(OUTPUTS_DIR, "trajectories_grid.png")
    fig3.savefig(path3, dpi=120, facecolor="white", bbox_inches="tight")
    print(f"  저장: {path3}")
    plt.close(fig3)

    # ── 그림 2: 전체 오버레이 ──────────────────────────
    fig4, ax4 = plt.subplots(figsize=(11, 5))

    ax4.axhspan(-threshold_deg, threshold_deg,
                color=XL_GREEN, alpha=0.06)
    ax4.axhline( threshold_deg, color=XL_GREEN,
                 linewidth=0.8, linestyle='--')
    ax4.axhline(-threshold_deg, color=XL_GREEN,
                 linewidth=0.8, linestyle='--')
    ax4.axvspan(0, burn_cutoff, color=XL_YELLOW, alpha=0.06)
    ax4.axvline(burn_cutoff, color=XL_YELLOW,
                linewidth=1.0, linestyle=':')

    cmap_s = plt.cm.Blues
    cmap_u = plt.cm.Reds
    s_idx  = [i for i, (_, r) in enumerate(traj_list) if r["stable"]]
    u_idx  = [i for i, (_, r) in enumerate(traj_list) if not r["stable"]]

    for rank, idx in enumerate(s_idx):
        phi0_deg, res_i = traj_list[idx]
        c = cmap_s(0.45 + 0.45 * rank / max(len(s_idx) - 1, 1))
        ax4.plot(res_i["time"], res_i["phi"], color=c,
                 linewidth=1.6, alpha=0.9,
                 label=f"phi0={phi0_deg:+.0f}deg")
        ax4.scatter([0], [phi0_deg], color=c, s=30, zorder=5)

    for rank, idx in enumerate(u_idx):
        phi0_deg, res_i = traj_list[idx]
        c = cmap_u(0.45 + 0.45 * rank / max(len(u_idx) - 1, 1))
        ax4.plot(res_i["time"], res_i["phi"], color=c,
                 linewidth=1.3, alpha=0.8, linestyle='--',
                 label=f"phi0={phi0_deg:+.0f}deg X")
        ax4.scatter([0], [phi0_deg], color=c, s=30, zorder=5)

    ax4.legend(fontsize=8, loc='upper right', ncol=2)
    ax4.set_title("전체 궤적 오버레이  |  실선=안정, 점선=불안정")
    ax4.set_xlabel("Time [s]")
    ax4.set_ylabel("Roll angle [deg]")

    fig4.tight_layout()
    path4 = os.path.join(OUTPUTS_DIR, "trajectories_overlay.png")
    fig4.savefig(path4, dpi=120, facecolor="white")
    print(f"  저장: {path4}")
    plt.close(fig4)


# ─────────────────────────────────────────
# 최종 결과 그래프 저장
# ─────────────────────────────────────────
def save_final_plot(sim_before, sim_after, stats_before, stats_after,
                    opt_study, best_kp, best_kd):

    from utils import style_ax as _style_ax

    fig = plt.figure(figsize=(14, 10))
    gs  = gridspec.GridSpec(3, 3, figure=fig,
                            hspace=0.45, wspace=0.35)

    t = sim_after["time"]

    # ── Row 0: 최적화 전/후 roll angle ──
    ax0 = fig.add_subplot(gs[0, 0])
    ax0.plot(sim_before["time"], sim_before["phi"],
             color=XL_RED, linewidth=1.5)
    ax0.axhline(0, color=XL_GRAY, linewidth=0.6, linestyle='--')
    _style_ax(ax0, "Roll angle — Before opt.")
    ax0.set_ylabel("φ [deg]", fontsize=8)
    ax0.set_xlabel("Time [s]", fontsize=8)

    ax1 = fig.add_subplot(gs[0, 1])
    ax1.plot(t, sim_after["phi"], color=XL_BLUE, linewidth=1.5)
    ax1.axhline(0, color=XL_GRAY, linewidth=0.6, linestyle='--')
    if sim_after["settling_time"] > 0:
        ax1.axvline(sim_after["settling_time"], color=XL_YELLOW,
                    linewidth=1, linestyle='--')
    _style_ax(ax1, "Roll angle — After opt.")
    ax1.set_ylabel("φ [deg]", fontsize=8)
    ax1.set_xlabel("Time [s]", fontsize=8)

    ax2 = fig.add_subplot(gs[0, 2])
    ax2.plot(t, sim_after["V"], color=XL_YELLOW, linewidth=1.5)
    _style_ax(ax2, "Velocity Profile")
    ax2.set_ylabel("V [m/s]", fontsize=8)
    ax2.set_xlabel("Time [s]", fontsize=8)

    # ── Row 1: 핀 명령 + MC 비교 ──
    ax3 = fig.add_subplot(gs[1, 0])
    ax3.plot(t, sim_after["delta"], color=XL_GREEN, linewidth=1.5)
    _style_ax(ax3, "Fin command δ (After opt.)")
    ax3.set_ylabel("δ [deg]", fontsize=8)
    ax3.set_xlabel("Time [s]", fontsize=8)

    ax4 = fig.add_subplot(gs[1, 1])
    raw_b = stats_before["raw"]
    raw_a = stats_after["raw"]
    err_b = [r["final_error"] for r in raw_b]
    err_a = [r["final_error"] for r in raw_a]
    ax4.hist(err_b, bins=20, color=XL_RED,  alpha=0.55,
             label=f"Before ({stats_before['success_rate']:.0f}%)")
    ax4.hist(err_a, bins=20, color=XL_BLUE, alpha=0.55,
             label=f"After  ({stats_after['success_rate']:.0f}%)")
    ax4.axvline(5.0, color=XL_YELLOW, linewidth=1, linestyle='--')
    _style_ax(ax4, "Final Error Distribution (MC)")
    ax4.set_xlabel("Final error [deg]", fontsize=8)
    ax4.set_ylabel("Count", fontsize=8)
    ax4.legend(fontsize=7)

    # ── BO 수렴 곡선 ──
    ax5 = fig.add_subplot(gs[1, 2])
    trials = opt_study.trials
    vals   = [tr.value for tr in trials if tr.value is not None]
    best_curve = []
    cur = float('inf')
    for v in vals:
        cur = min(cur, v)
        best_curve.append(cur)
    ax5.plot(vals, color=XL_BLUE, alpha=0.4, linewidth=1)
    ax5.plot(best_curve, color=XL_ORANGE, linewidth=2)
    _style_ax(ax5, "BO Convergence")
    ax5.set_xlabel("Trial", fontsize=8)
    ax5.set_ylabel("Score J", fontsize=8)

    # ── Row 2: 성능 요약 텍스트 ──
    ax6 = fig.add_subplot(gs[2, :])
    ax6.axis('off')

    rows_data = [
        ("성공률",
         f"{stats_before['success_rate']:.1f}%",
         f"{stats_after['success_rate']:.1f}%"),
        ("평균 안정화 시간",
         f"{stats_before['mean_settling']:.2f} s" if stats_before['mean_settling'] > 0 else "N/A",
         f"{stats_after['mean_settling']:.2f} s"),
        ("평균 최대 초과각",
         f"{stats_before['mean_overshoot']:.1f} deg",
         f"{stats_after['mean_overshoot']:.1f} deg"),
        ("평균 최종 오차",
         f"{stats_before['mean_error']:.2f} deg",
         f"{stats_after['mean_error']:.2f} deg"),
    ]

    headers = ["항목", "최적화 전", "최적화 후"]
    col_x   = [0.05, 0.40, 0.70]
    y_start = 0.85
    dy      = 0.18

    for j, h in enumerate(headers):
        ax6.text(col_x[j], y_start, h,
                 color="#262626", fontsize=9,
                 fontweight='bold', transform=ax6.transAxes)

    for i, (label, before, after) in enumerate(rows_data):
        y = y_start - (i + 1) * dy
        ax6.text(col_x[0], y, label,
                 color="#595959", fontsize=9, transform=ax6.transAxes)
        ax6.text(col_x[1], y, before,
                 color=XL_RED, fontsize=9, transform=ax6.transAxes)
        ax6.text(col_x[2], y, after,
                 color=XL_BLUE, fontsize=9,
                 fontweight='bold', transform=ax6.transAxes)

    gain_text = (f"동압 정규화  |  "
                 f"Kp_base={best_kp:.4f}   "
                 f"Kd_base={best_kd:.4f}   "
                 f"(V=20 m/s 기준, 다른 속도에서 자동 조정)")
    ax6.text(0.05, 0.05, gain_text,
             color=XL_GREEN, fontsize=8, transform=ax6.transAxes)

    fig.suptitle("Rocket Roll Control — Simulation Results",
                 fontsize=13, y=0.98)

    plt.savefig(RESULT_PNG, dpi=130, facecolor="white",
                bbox_inches='tight')
    print(f"\n  최종 결과 저장: final_result.png")


# ─────────────────────────────────────────
# 메인 실행
# ─────────────────────────────────────────
if __name__ == "__main__":
    print("\n" + "🚀 " * 10)
    print("  Rocket Roll Control Simulation")
    print("🚀 " * 10)

    # Step 1
    step1_model_check()

    # Step 2
    res_basic = step2_controller_check()

    # Step 3 (최적화 전 시뮬레이터)
    res_before = step3_simulator_check()

    # Step 4 (최적화 전 MC)
    stats_before = step4_monte_carlo_before()

    # Step 5 (Bayesian Optimization)
    opt_result  = step5_optimization()

    best_kp     = opt_result["Kp_base"]
    best_kd     = opt_result["Kd_base"]
    stats_after = opt_result["final_stats"]
    study       = opt_result["study"]

    # ── 3D 최적화 표면 생성 ──
    print(f"\n{'='*55}")
    print(f"  3D 최적화 표면 생성 중...")
    print(f"{'='*55}")
    fig_3d = visualize_3d_optimization_surface(study)
    path_3d = os.path.join(OUTPUTS_DIR, "optimization_3d_surface.png")
    fig_3d.savefig(path_3d, dpi=120, facecolor="white", bbox_inches='tight')
    print(f"  저장: {path_3d}")
    plt.close(fig_3d)

    # 최적 gain으로 단일 시뮬레이션
    rocket_final = RocketModel()
    ctrl_final   = PDController(Kp_base=best_kp, Kd_base=best_kd)
    sim_final    = Simulator(rocket=rocket_final, controller=ctrl_final,
                             dt=DT, T=T_SIM)
    res_after    = sim_final.run(phi0=np.deg2rad(PHI0_DEG))

    # 다중 궤적 그래프 저장
    save_trajectory_plots(best_kp=best_kp, best_kd=best_kd)

    # 최종 그래프 저장
    save_final_plot(
        sim_before   = res_before,
        sim_after    = res_after,
        stats_before = stats_before,
        stats_after  = stats_after,
        opt_study    = study,
        best_kp      = best_kp,
        best_kd      = best_kd,
    )

    # 최종 요약
    separator("완료!")
    print(f"  성공률  {stats_before['success_rate']:.1f}%  →  "
          f"{stats_after['success_rate']:.1f}%  🎯")
    print(f"\n  저장된 파일:")
    print(f"    final_result.png")
    print(f"    trajectories_grid.png")
    print(f"    trajectories_overlay.png")
    print(f"    optimization_3d_surface.png  ← 3D 최적화 표면 & 등고선도")
    print(f"\n  최적 gain (controller.py에 반영):")
    print(f"    KP_BASE = {best_kp:.4f}")
    print(f"    KD_BASE = {best_kd:.4f}")
    print(f"    (V=20 m/s 기준, 다른 속도에서 자동 조정)")
    print()
