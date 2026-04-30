"""
simulator.py
------------
전체 시뮬레이션 루프

기능:
    - 실측 추력 데이터 기반 속도 프로파일
    - 공기저항 (Drag) 포함
    - 연료 질량 감소 (연소 중 m(t) 변화)
    - Gain scheduling 자동 적용
    - 결과 로그 및 성능 지표 계산

성능 지표:
    - settling_time : φ가 ±15도 이내로 들어온 시간 [s]
    - max_overshoot : 최대 초과 각도 [deg]
    - final_error   : 최종 roll 각도 오차 [deg]
    - stable        : 최종적으로 안정화 여부 (True/False)
"""

import numpy as np


# ─────────────────────────────────────────
# 실측 추력 데이터 (1차 추력 실험)
# ─────────────────────────────────────────
THRUST_TIME = np.array([
    0, 0.08, 0.17, 0.25, 0.34, 0.42, 0.5, 0.59, 0.67, 0.76,
    0.84, 0.93, 1.01, 1.09, 1.18, 1.26, 1.35, 1.43, 1.51, 1.6,
    1.68, 1.77, 1.85, 1.93, 2.02, 2.1, 2.19, 2.27, 2.35, 2.44,
    2.52, 2.61, 2.69, 2.77, 2.86, 2.94, 3.03, 3.11, 3.19, 3.28,
    3.36, 3.45, 3.53, 3.61, 3.7, 3.78, 3.87, 3.95, 4.03
])  # [s]

THRUST_FORCE = np.array([
    0.18, 0.2, 0.24, 0.3, 0.42, 1.57, 15.92, 46.46, 61.96, 73.52,
    92.68, 105.72, 113.75, 128.58, 170.08, 228.08, 261.92, 284.41,
    304.08, 324.49, 342.06, 344.64, 326.36, 282.74, 223.42, 174.64,
    140.11, 114.02, 95.18, 82.56, 70.66, 61.63, 54.34, 45.61, 37.41,
    29.98, 20.5, 13.69, 8.89, 6.02, 4.21, 2.61, 2.02, 1.77, 1.6,
    1.46, 1.36, 1.33, 1.3
])  # [N]

# ─────────────────────────────────────────
# 로켓 비행 파라미터
# ─────────────────────────────────────────
ROCKET_MASS_TOTAL = 5.3    # [kg]  발사 시 총 질량 (연료 포함) — 실측
FUEL_MASS         = 0.4    # [kg]  연료 질량 — 실측
ROCKET_MASS_DRY   = ROCKET_MASS_TOTAL - FUEL_MASS  # [kg] 건조 질량 = 4.9kg
ROCKET_RADIUS     = 0.052  # [m]   동체 반지름 (외경 104mm / 2) — 실측
G                 = 9.81   # [m/s²]

# ─────────────────────────────────────────
# 공기저항 파라미터
# ─────────────────────────────────────────
CD        = 0.49                             # [-]   항력계수 — 실측
A_REF     = np.pi * ROCKET_RADIUS ** 2      # [m²]  기준 단면적 = π×0.05² ≈ 0.00785
RHO_AIR   = 1.225                           # [kg/m³] 공기밀도 (해수면)

# 연소 종료 시간
BURN_END  = THRUST_TIME[-1]   # ≈ 4.03s


def thrust_force(t: float) -> float:
    """시간 t에서 추력 [N] — 실측 데이터 보간"""
    if t < THRUST_TIME[0] or t > THRUST_TIME[-1]:
        return 0.0
    return float(np.interp(t, THRUST_TIME, THRUST_FORCE))


def rocket_mass(t: float) -> float:
    """
    시간 t에서 로켓 질량 [kg]

    연소 중: 연료가 선형적으로 감소
        m(t) = m_total - (fuel_mass / burn_time) × t

    연소 후: 건조 질량 고정
        m(t) = m_dry
    """
    if t <= 0:
        return ROCKET_MASS_TOTAL
    if t >= BURN_END:
        return ROCKET_MASS_DRY
    burn_time = BURN_END - THRUST_TIME[0]
    m_dot = FUEL_MASS / burn_time          # [kg/s] 연료 소모율
    return max(ROCKET_MASS_DRY, ROCKET_MASS_TOTAL - m_dot * t)


def drag_force(V: float) -> float:
    """
    공기저항 [N]
    F_drag = 0.5 × rho × V² × Cd × A
    항상 속도 반대 방향
    """
    return 0.5 * RHO_AIR * (V ** 2) * CD * A_REF


def _compute_velocity_profile(dt: float = 0.005) -> tuple:
    """
    추력 실험 데이터로 속도 프로파일 계산

    F_net = F_thrust - m(t)×g - F_drag(V)
    a     = F_net / m(t)
    V     = ∫ a dt

    공기저항 + 연료 질량 감소 모두 반영
    반환: (time_arr, velocity_arr, mass_arr)
    """
    T_total = THRUST_TIME[-1] + 6.0   # 연소 후 충분한 시간
    t_arr   = np.arange(0, T_total, dt)
    v_arr   = np.zeros(len(t_arr))
    m_arr   = np.zeros(len(t_arr))
    v       = 0.0

    for i in range(1, len(t_arr)):
        t    = t_arr[i - 1]
        F    = thrust_force(t)
        m    = rocket_mass(t)
        Fd   = drag_force(v)                    # 공기저항
        a    = (F - m * G - Fd) / m            # 순 가속도
        v   += a * dt
        v    = max(v, 1.0)                      # 최소 속도 1 m/s
        v_arr[i] = v
        m_arr[i] = m

    m_arr[0] = ROCKET_MASS_TOTAL
    return t_arr, v_arr, m_arr


# 시뮬레이션 시작 시 한 번만 계산
_VEL_TIME, _VEL_ARR, _MASS_ARR = _compute_velocity_profile()


def velocity_profile(t: float) -> float:
    """
    시간 t [s] 에서의 비행 속도 [m/s]
    공기저항 + 연료 질량 감소 반영
    """
    if t <= _VEL_TIME[0]:
        return float(_VEL_ARR[0])
    if t >= _VEL_TIME[-1]:
        return float(_VEL_ARR[-1])
    return float(np.interp(t, _VEL_TIME, _VEL_ARR))


class Simulator:
    """
    시뮬레이션 루프

    사용 예:
        sim = Simulator()
        results = sim.run(phi0=np.deg2rad(30))
        print(results['settling_time'])
    """

    def __init__(self,
                 rocket=None,
                 controller=None,
                 dt: float = 0.01,
                 T: float = 10.0,
                 stable_threshold: float = 5.0):   # +-5도
        """
        Parameters
        ----------
        rocket           : RocketModel 인스턴스
        controller       : PDController 인스턴스
        dt               : 타임스텝 [s]
        T                : 시뮬레이션 총 시간 [s]
        stable_threshold : 안정화 판정 각도 임계값 [deg]
        """
        from rocket_model import RocketModel
        from controller import PDController

        self.rocket     = rocket     or RocketModel()
        self.controller = controller or PDController()
        self.dt         = dt
        self.T          = T
        self.threshold  = np.deg2rad(stable_threshold)

    # ─────────────────────────────────────
    # 공개 메서드
    # ─────────────────────────────────────

    def run(self,
            phi0: float = np.deg2rad(30),
            p0: float = 0.0,
            disturbance_fn=None,
            V_profile=None) -> dict:
        """
        시뮬레이션 실행

        Parameters
        ----------
        phi0            : 초기 roll 각도 [rad]
        p0              : 초기 roll 각속도 [rad/s]
        disturbance_fn  : 외란 함수 f(t) → 토크 [N·m]
                          None이면 외란 없음
        V_profile       : 속도 함수 f(t) → V [m/s]
                          None이면 기본 velocity_profile 사용

        Returns
        -------
        dict: {
            'time'          : 시간 배열
            'phi'           : roll 각도 [deg]
            'p'             : roll 각속도 [deg/s]
            'delta'         : 핀 각도 [deg]
            'V'             : 속도 [m/s]
            'settling_time' : 안정화 시간 [s]  (-1이면 미안정)
            'max_overshoot' : 최대 초과 [deg]
            'final_error'   : 최종 오차 [deg]
            'stable'        : 안정화 여부
        }
        """
        # 초기화
        self.rocket.reset(phi0=phi0, p0=p0)
        V_fn    = V_profile or velocity_profile
        dist_fn = disturbance_fn or (lambda t: 0.0)

        # 연소 종료 시간 (추력 < 5N 기준)
        BURN_CUTOFF = 0; #THRUST_TIME[-1] - 3.5   # ≈ 0.5s 제어 시작 //처음부터 제어 시작

        # 로그 배열
        time_arr  = np.arange(0, self.T, self.dt)
        phi_log   = np.zeros(len(time_arr))
        p_log     = np.zeros(len(time_arr))
        delta_log = np.zeros(len(time_arr))
        V_log     = np.zeros(len(time_arr))

        settling_time = -1.0
        stable_count  = 0
        STABLE_STEPS  = 50     # 0.5초 연속 안정이면 확정

        # ── 메인 루프 ──
        for i, t in enumerate(time_arr):
            phi, p = self.rocket.state
            V      = V_fn(t)
            dist   = dist_fn(t)

            # 연소 중에는 핀 고정 (제어 불가 구간)
            # 연소 후에만 PD 제어기 활성화
            if t < BURN_CUTOFF:
                u = 0.0   # 핀 중립
            else:
                u = self.controller.update(phi=phi, p=p, V=V)

            # 물리 전진
            self.rocket.step(delta_cmd=u, V=V, dt=self.dt,
                             disturbance=dist)

            # 로그
            phi_log[i]   = np.rad2deg(phi)
            p_log[i]     = np.rad2deg(p)
            delta_log[i] = np.rad2deg(u)
            V_log[i]     = V

            # 안정화 판정 (연소 후 구간만)
            if t >= BURN_CUTOFF and settling_time < 0:
                if abs(phi) < self.threshold:
                    stable_count += 1
                    if stable_count >= STABLE_STEPS:
                        settling_time = t
                else:
                    stable_count = 0

        # ── 성능 지표 계산 ──
        max_overshoot    = float(np.max(np.abs(phi_log)))
        final_error      = float(abs(phi_log[-1]))
        threshold_deg    = np.rad2deg(self.threshold)
        stable           = final_error < threshold_deg

        return {
            "time"          : time_arr,
            "phi"           : phi_log,
            "p"             : p_log,
            "delta"         : delta_log,
            "V"             : V_log,
            "settling_time" : settling_time,
            "max_overshoot" : max_overshoot,
            "final_error"   : final_error,
            "stable"        : stable,
        }

    def score(self, results: dict) -> float:
        """
        성능 점수 계산 (낮을수록 좋음)
        Bayesian Optimization의 목적함수로 사용
        """
        if not results["stable"]:
            return 1000.0

        J = (results["max_overshoot"] * 0.5 +
             results["settling_time"] * 10.0 +
             results["final_error"]   * 2.0)
        return float(J)


# ─────────────────────────────────────────
# 단독 실행 테스트
# ─────────────────────────────────────────
if __name__ == "__main__":
    import matplotlib.pyplot as plt
    import sys
    import os
    sys.path.insert(0, os.path.dirname(__file__))
    from utils import set_korean_font, set_excel_style, XL_BLUE, XL_RED, XL_GREEN, XL_YELLOW, XL_GRAY
    set_korean_font()
    set_excel_style()

    from rocket_model import RocketModel
    from controller   import PDController

    # 시뮬레이터 구성
    rocket = RocketModel()
    ctrl   = PDController(use_scheduling=True)
    sim    = Simulator(rocket=rocket, controller=ctrl, T=5.0)

    # 실행 (초기 roll 30도, 외란 없음)
    results = sim.run(phi0=np.deg2rad(30))

    # 성능 출력
    print("=" * 40)
    print("  시뮬레이션 결과")
    print("=" * 40)
    print(f"  안정화 여부  : {'✅ 안정' if results['stable'] else '❌ 불안정'}")
    print(f"  안정화 시간  : {results['settling_time']:.2f} s"
          if results['settling_time'] > 0 else "  안정화 시간  : 미달성")
    print(f"  최대 초과각  : {results['max_overshoot']:.2f} deg")
    print(f"  최종 오차    : {results['final_error']:.3f} deg")
    print(f"  성능 점수 J  : {sim.score(results):.2f}  (낮을수록 좋음)")
    print("=" * 40)

    # 그래프
    t   = results["time"]
    fig, axes = plt.subplots(4, 1, figsize=(9, 9), sharex=True)

    colors = [XL_BLUE, XL_RED, XL_GREEN, XL_YELLOW]
    labels = ["Roll angle φ [deg]", "Roll rate p [deg/s]",
              "Fin cmd δ [deg]",    "Speed V [m/s]"]
    keys   = ["phi", "p", "delta", "V"]

    for ax, key, color, label in zip(axes, keys, colors, labels):
        ax.plot(t, results[key], color=color, linewidth=1.8)
        ax.set_ylabel(label, fontsize=9)
        ax.axhline(0, color=XL_GRAY, linewidth=0.6, linestyle='--')

    # 안정화 시간 표시
    if results['settling_time'] > 0:
        axes[0].axvline(results['settling_time'], color=XL_YELLOW,
                        linewidth=1, linestyle='--')
        axes[0].text(results['settling_time'] + 0.05,
                     results['phi'].max() * 0.8,
                     f"t={results['settling_time']:.2f}s",
                     color=XL_YELLOW, fontsize=8)

    axes[0].set_title("Simulator Test  |  phi0=30deg, Gain Scheduling ON")
    axes[-1].set_xlabel("Time [s]")

    plt.tight_layout()
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "outputs")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "simulator_test.png")
    plt.savefig(out_path, dpi=120, facecolor="white")
    print(f"  그래프 저장: {out_path}")

    # ── 다중 랜덤 초기각도 궤적 ──────────────────────────
    # phi0를 균등하게 분산시켜 대표적인 케이스를 선택
    PHI0_SAMPLES_DEG = [-90, -70, -50, -30, 20, 40, 60, 80]
    N_TRAJ = len(PHI0_SAMPLES_DEG)

    print(f"\n  초기각도 {N_TRAJ}개 궤적 시뮬레이션 중...")

    traj_list = []
    for phi0_deg in PHI0_SAMPLES_DEG:
        r_i   = RocketModel()
        c_i   = PDController(use_scheduling=True)
        s_i   = Simulator(rocket=r_i, controller=c_i, T=5.0)
        res_i = s_i.run(phi0=np.deg2rad(phi0_deg))
        traj_list.append((phi0_deg, res_i))
        status = "안정" if res_i["stable"] else "불안정"
        print(f"    φ₀={phi0_deg:+5.0f}°  →  {status}"
              f"  settling={res_i['settling_time']:.2f}s"
              f"  final_err={res_i['final_error']:.2f}°")

    threshold_deg = np.rad2deg(Simulator().threshold)
    burn_cutoff   = THRUST_TIME[-1] - 3.5   # BURN_CUTOFF와 동일

    # ── 그림 1: 4×2 그리드 (각 케이스별 roll angle + fin command) ──
    NCOLS = 2
    NROWS = (N_TRAJ + NCOLS - 1) // NCOLS   # ceil(N/2)

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

        # roll angle (좌축)
        ax.plot(t, res_i["phi"], color=c_phi, linewidth=1.6,
                label="φ roll [deg]")
        ax.axhspan(-threshold_deg, threshold_deg,
                   color=XL_GREEN, alpha=0.06)
        ax.axhline( threshold_deg, color=XL_GREEN,
                    linewidth=0.8, linestyle='--')
        ax.axhline(-threshold_deg, color=XL_GREEN,
                    linewidth=0.8, linestyle='--')

        # fin command (우축)
        ax2b = ax.twinx()
        ax2b.plot(t, res_i["delta"], color=c_fin,
                  linewidth=1.2, linestyle='--', alpha=0.8,
                  label="δ fin [deg]")
        ax2b.set_ylabel("Fin δ [deg]", color=c_fin, fontsize=8)
        ax2b.tick_params(colors=c_fin, labelsize=7)
        ax2b.spines["right"].set_edgecolor(c_fin)
        for sp in ["top", "left", "bottom"]:
            ax2b.spines[sp].set_visible(False)

        # 연소 구간 음영
        ax.axvspan(0, burn_cutoff, color=XL_YELLOW, alpha=0.06)
        ax.axvline(burn_cutoff, color=XL_YELLOW,
                   linewidth=0.8, linestyle=':')

        # 안정화 시간 표시
        if res_i["settling_time"] > 0:
            ax.axvline(res_i["settling_time"], color=XL_GRAY,
                       linewidth=0.9, linestyle='--')

        label_stable = "O stable" if stable else "X unstable"
        ax.set_title(
            f"phi0 = {phi0_deg:+.0f} deg   [{label_stable}]"
            f"   err={res_i['final_error']:.1f} deg",
            fontsize=9, pad=4)
        ax.set_ylabel("Roll φ [deg]", fontsize=8)
        ax.tick_params(labelsize=7)

        if row == NROWS - 1:
            ax.set_xlabel("Time [s]", fontsize=8)

    # 빈 칸 숨기기
    for idx in range(N_TRAJ, NROWS * NCOLS):
        row, col = divmod(idx, NCOLS)
        axes3[row][col].set_visible(False)

    n_stable = sum(1 for _, r in traj_list if r["stable"])
    fig3.suptitle(
        f"초기각도별 Roll 제어 궤적  |  {n_stable}/{N_TRAJ} 안정화"
        f"   (파란선=roll, 초록점선=fin, 노란음영=연소)",
        fontsize=11, y=1.01)
    fig3.tight_layout()
    out_path3 = os.path.join(out_dir, "trajectories_grid.png")
    fig3.savefig(out_path3, dpi=120, facecolor="white",
                 bbox_inches="tight")
    print(f"  그래프 저장: {out_path3}")

    # ── 그림 2: 전체 오버레이 (roll angle 한 장에) ──────────
    fig4, ax4 = plt.subplots(figsize=(11, 5))

    ax4.axhspan(-threshold_deg, threshold_deg,
                color=XL_GREEN, alpha=0.06)
    ax4.axhline( threshold_deg, color=XL_GREEN,
                 linewidth=0.8, linestyle='--')
    ax4.axhline(-threshold_deg, color=XL_GREEN,
                 linewidth=0.8, linestyle='--')
    ax4.axvspan(0, burn_cutoff, color=XL_YELLOW, alpha=0.06,
                label="연소 구간")
    ax4.axvline(burn_cutoff, color=XL_YELLOW,
                linewidth=1.0, linestyle=':')

    cmap_stable   = plt.cm.Blues
    cmap_unstable = plt.cm.Reds
    stable_idx    = [i for i,(_, r) in enumerate(traj_list) if r["stable"]]
    unstable_idx  = [i for i,(_, r) in enumerate(traj_list) if not r["stable"]]

    for rank, idx in enumerate(stable_idx):
        phi0_deg, res_i = traj_list[idx]
        c = cmap_stable(0.45 + 0.45 * rank / max(len(stable_idx)-1, 1))
        ax4.plot(res_i["time"], res_i["phi"], color=c,
                 linewidth=1.6, alpha=0.9,
                 label=f"phi0={phi0_deg:+.0f}deg")
        ax4.scatter([0], [phi0_deg], color=c, s=30, zorder=5)

    for rank, idx in enumerate(unstable_idx):
        phi0_deg, res_i = traj_list[idx]
        c = cmap_unstable(0.45 + 0.45 * rank / max(len(unstable_idx)-1, 1))
        ax4.plot(res_i["time"], res_i["phi"], color=c,
                 linewidth=1.3, alpha=0.8, linestyle='--',
                 label=f"phi0={phi0_deg:+.0f}deg X")
        ax4.scatter([0], [phi0_deg], color=c, s=30, zorder=5)

    ax4.legend(fontsize=8, loc='upper right', ncol=2)
    ax4.set_title("전체 궤적 오버레이  |  실선=안정, 점선=불안정  (점=시작 각도)")
    ax4.set_xlabel("Time [s]")
    ax4.set_ylabel("Roll angle φ [deg]")

    fig4.tight_layout()
    out_path4 = os.path.join(out_dir, "trajectories_overlay.png")
    fig4.savefig(out_path4, dpi=120, facecolor="white")
    print(f"  그래프 저장: {out_path4}")

    plt.show()
