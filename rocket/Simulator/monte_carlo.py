"""
monte_carlo.py
--------------
Monte Carlo 기반 robustness 평가

랜덤 요소:
    - wind_std    : 바람 외란 토크 표준편차 [N·m]
    - mass_var    : 질량 변동률  (예: 0.05 = ±5%)
    - cg_shift    : CG 이동으로 인한 관성모멘트 변동률
    - noise_std   : 센서 노이즈 표준편차 [rad]
    - servo_delay : 서보 추가 지연 [s]

출력 지표:
    - success_rate  : 안정화 성공률 [%]
    - mean_settling : 평균 안정화 시간 [s]
    - mean_overshoot: 평균 최대 초과각 [deg]
    - mean_error    : 평균 최종 오차 [deg]
"""

import numpy as np
from copy import deepcopy


class MonteCarlo:
    """
    Monte Carlo 시뮬레이션

    사용 예:
        mc = MonteCarlo(n_runs=500)
        stats = mc.run()
        print(f"성공률: {stats['success_rate']:.1f}%")
    """

    def __init__(self,
                 n_runs: int = 500,
                 rocket_params: dict = None,
                 Kp_base: float = None,
                 Kd_base: float = None,
                 gain_schedule: list = None,
                 disturbance_params: dict = None,
                 dt: float = 0.01,
                 T: float = 10.0,
                 verbose: bool = True):
        self.n_runs        = n_runs
        self.base_params   = rocket_params
        self.Kp_base       = Kp_base
        self.Kd_base       = Kd_base
        self.gain_schedule = gain_schedule  # 하위 호환성
        self.dt            = dt
        self.T             = T
        self.verbose       = verbose

        # ── 외란 파라미터 기본값 ──
        default_dist = {
            "wind_std"    : 0.003,   # [N·m]  바람 토크 표준편차
                                     # V=10m/s 제어 토크(0.00443 N·m)의 ~23%
                                     # (Cl_alpha=2.26 기준)
            "mass_var"    : 0.03,    # [ratio] 질량 변동 ±3%
            "cg_var"      : 0.02,    # [ratio] 관성모멘트 변동 ±2%
            "noise_std"   : np.deg2rad(0.3),  # [rad] 센서 노이즈 0.3도
            "servo_delay" : 0.002,   # [s]    서보 추가 지연
            "phi0_std"    : np.deg2rad(3),    # [rad] 초기각도 불확실성 3도
            "phi0_min" : np.deg2rad(-90),    # 추가
            "phi0_max" : np.deg2rad(90),   # 추가
            # 추력 편심 제외:
            #   분석 결과 연소 중 편심 토크(0.030 N·m) >>
            #   핀 제어 토크(0.0044 N·m) 이므로
            #   연소 후 관성 비행 구간만 제어 대상으로 설정
        }
        self.dist_params = {**default_dist, **(disturbance_params or {})}

    # ─────────────────────────────────────
    # 공개 메서드
    # ─────────────────────────────────────

    def run(self, phi0_mean: float = np.deg2rad(30),
            Kp: float = None, Kd: float = None) -> dict:
        """
        Monte Carlo 실행

        Parameters
        ----------
        phi0_mean : 초기 roll 각도 평균 [rad]
        Kp, Kd    : 고정 gain (None이면 gain scheduling 사용)

        Returns
        -------
        dict: 통계 결과
        """
        from rocket_model import RocketModel
        from controller   import PDController
        from simulator    import Simulator, velocity_profile

        results_list = []

        for i in range(self.n_runs):
            if self.verbose and (i % 100 == 0):
                print(f"  [{i:4d}/{self.n_runs}] 진행 중...")

            # ── 랜덤 파라미터 샘플링 ──
            rng = np.random

            # 질량/관성 변동
            mass_factor = 1.0 + rng.uniform(-self.dist_params["mass_var"],
                                             self.dist_params["mass_var"])
            cg_factor   = 1.0 + rng.uniform(-self.dist_params["cg_var"],
                                             self.dist_params["cg_var"])

            # 파라미터 변형
            params = dict(self.base_params) if self.base_params else {}
            if "I" in params:
                params["I"] = params["I"] * mass_factor * cg_factor
            else:
                from rocket_model import DEFAULT_PARAMS
                params = dict(DEFAULT_PARAMS)
                params["I"] = params["I"] * mass_factor * cg_factor

            # 서보 지연 추가
            params["tau_servo"] = params.get("tau_servo", 0.03) + \
                                   abs(rng.normal(0, self.dist_params["servo_delay"]))

            # 초기 각도 불확실성 (phi0_mean 중심으로 균등 분포)
            half_range = (self.dist_params["phi0_max"]
                          - self.dist_params["phi0_min"]) / 2.0
            phi0 = rng.uniform(phi0_mean - half_range,
                               phi0_mean + half_range)

            # 외란 함수 (바람만)
            wind_std   = self.dist_params["wind_std"]
            noise_std  = self.dist_params["noise_std"]
            wind_force = rng.normal(0, wind_std)

            def disturbance_fn(t, _w=wind_force, _wstd=wind_std * 0.1):
                # 바람: 일정 성분 + 터뷸런스
                return _w + rng.normal(0, _wstd)

            # 센서 노이즈 포함 제어기
            noise_s = noise_std

            # 모델 / 제어기 / 시뮬레이터 생성
            rocket = RocketModel(params=params)

            if Kp is not None and Kd is not None:
                ctrl = PDController(Kp=Kp, Kd=Kd, use_scheduling=False)
            elif self.Kp_base is not None and self.Kd_base is not None:
                ctrl = PDController(Kp_base=self.Kp_base,
                                    Kd_base=self.Kd_base)
            else:
                ctrl = PDController()

            # 센서 노이즈 주입을 위해 update 래핑
            original_update = ctrl.update
            p_noise_s = np.deg2rad(0.05)   # IMU 각속도 노이즈 ~0.05 deg/s
            def noisy_update(phi, p, V=50.0,
                             _fn=original_update, _ns=noise_s,
                             _pns=p_noise_s):
                phi_noisy = phi + rng.normal(0, _ns)
                p_noisy   = p   + rng.normal(0, _pns)
                return _fn(phi=phi_noisy, p=p_noisy, V=V)
            ctrl.update = noisy_update

            sim = Simulator(rocket=rocket, controller=ctrl,
                            dt=self.dt, T=self.T)

            res = sim.run(phi0=phi0, disturbance_fn=disturbance_fn)
            results_list.append(res)

        # ── 통계 집계 ──
        return self._aggregate(results_list)

    # ─────────────────────────────────────
    # 내부 메서드
    # ─────────────────────────────────────

    def _aggregate(self, results_list: list) -> dict:
        """결과 리스트 → 통계 dict"""
        n = len(results_list)

        stable_arr    = np.array([r["stable"]        for r in results_list])
        settling_arr  = np.array([r["settling_time"] for r in results_list
                                   if r["stable"]])
        overshoot_arr = np.array([r["max_overshoot"] for r in results_list])
        error_arr     = np.array([r["final_error"]   for r in results_list])

        success_rate   = stable_arr.sum() / n * 100.0
        mean_settling  = float(settling_arr.mean()) if len(settling_arr) else -1.0
        mean_overshoot = float(overshoot_arr.mean())
        mean_error     = float(error_arr.mean())
        std_error      = float(error_arr.std())

        stats = {
            "n_runs"        : n,
            "success_rate"  : success_rate,
            "n_stable"      : int(stable_arr.sum()),
            "mean_settling" : mean_settling,
            "mean_overshoot": mean_overshoot,
            "mean_error"    : mean_error,
            "std_error"     : std_error,
            "raw"           : results_list,   # 전체 raw 데이터
        }
        return stats


def print_stats(stats: dict):
    """통계 결과 출력"""
    print("=" * 45)
    print("  Monte Carlo 결과")
    print("=" * 45)
    print(f"  실행 횟수      : {stats['n_runs']} runs")
    print(f"  성공률         : {stats['success_rate']:.1f}%  "
          f"({stats['n_stable']}/{stats['n_runs']})")
    print(f"  평균 안정화 시간: {stats['mean_settling']:.2f} s")
    print(f"  평균 최대 초과각: {stats['mean_overshoot']:.2f} deg")
    print(f"  평균 최종 오차 : {stats['mean_error']:.3f} deg "
          f"(±{stats['std_error']:.3f})")
    print("=" * 45)


# ─────────────────────────────────────────
# 단독 실행 테스트
# ─────────────────────────────────────────
if __name__ == "__main__":
    import matplotlib.pyplot as plt
    import sys
    import os
    sys.path.insert(0, os.path.dirname(__file__))
    from utils import set_korean_font, set_excel_style, XL_BLUE, XL_RED, XL_GREEN, XL_YELLOW
    set_korean_font()
    set_excel_style()

    N = 200   # 테스트용 200회 (실제는 500~1000 권장)

    print(f"\nMonte Carlo {N}회 실행 중...")
    mc    = MonteCarlo(n_runs=N, verbose=True)
    stats = mc.run(phi0_mean=np.deg2rad(30))

    print_stats(stats)

    # ── 히스토그램 시각화 ──
    raw = stats["raw"]
    overshoot_arr = np.array([r["max_overshoot"] for r in raw])
    error_arr     = np.array([r["final_error"]   for r in raw])
    stable_arr    = np.array([r["stable"]        for r in raw])

    fig, axes = plt.subplots(1, 2, figsize=(10, 4))

    # 최대 초과각 분포
    axes[0].hist(overshoot_arr, bins=25, color=XL_BLUE, alpha=0.85)
    axes[0].set_xlabel("Max overshoot [deg]")
    axes[0].set_ylabel("Count")
    axes[0].set_title(f"Max Overshoot Distribution  (n={N})")

    # 최종 오차 분포
    c = [XL_GREEN if s else XL_RED for s in stable_arr]
    axes[1].scatter(range(N), error_arr, c=c, s=8, alpha=0.6)
    axes[1].axhline(5.0, color=XL_YELLOW, linewidth=1,
                    linestyle='--', label='5 deg threshold')
    axes[1].set_xlabel("Run index")
    axes[1].set_ylabel("Final error [deg]")
    axes[1].set_title(f"Final Error  |  Success {stats['success_rate']:.1f}%")
    axes[1].legend(fontsize=8)

    fig.suptitle("Monte Carlo Robustness Test", fontsize=12, y=1.01)
    plt.tight_layout()
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "outputs")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "monte_carlo_test.png")
    plt.savefig(out_path, dpi=120, facecolor="white")
    print(f"  그래프 저장: {out_path}")
