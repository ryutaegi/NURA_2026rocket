"""
rocket_model.py
---------------
1축 Roll dynamics 물리 모델

상태변수:
    phi   : roll 각도 [rad]
    p     : roll 각속도 [rad/s]
    delta : 현재 핀 각도 [rad]  (서보 출력)

운동방정식:
    I * dp/dt = tau_fin + tau_disturbance
    dphi/dt   = p

핀 토크:
    F   = 0.5 * rho * V^2 * Cl(delta) * S
    tau = F * l
"""

import numpy as np


# ─────────────────────────────────────────
# 기본 파라미터 (전국대학생 로켓대회 기준 예시값)
# 실제 로켓 스펙으로 교체하세요
# ─────────────────────────────────────────
DEFAULT_PARAMS = {
    # 관성 (m=5.3kg, r=0.052m 원기둥 근사: I = 0.5*m*r²)
    "I"             : 0.00717,         # [kg·m²]  롤 관성모멘트 — 0.5×5.3×0.052²

    # 공기역학 (움직이는 핀 2개 기준)
    "rho"           : 1.225,           # [kg/m³]  공기밀도 (해수면)
    "S"             : 0.00102,         # [m²]     조종면 면적 510mm²×2 — 실측
    "l"             : 0.172,           # [m]      조종면 중심~로켓 중심축 거리 — 실측
    "Cl_alpha"      : 2.26,            # [1/rad]  ← OpenRocket으로 재측정 권장
    "Cd_roll"       : 0.02,            # [-]      roll 댐핑 계수 (공기 마찰)

    # 서보 (Hitec HDS-877 @ 6.0V 기준, 2개)
    "tau_servo"     : 0.03,            # [s]      서보 1차 시상수
    "delta_max"     : np.deg2rad(15),  # [rad]    핀 최대 편향각 (±15도 권장)
    "rate_limit"    : 11.64,           # [rad/s]  0.09sec/60° 변환값
    "tau_servo_max" : 2.449 * 2,       # [N·m]    34.70 oz-in × 2개
}


class RocketModel:
    """
    1축 Roll 물리 모델

    사용 예:
        rocket = RocketModel()
        rocket.reset(phi0=np.deg2rad(30))   # 초기 roll 30도

        for t in time_array:
            phi, p = rocket.step(delta_cmd=u, V=50, dt=0.01)
    """

    def __init__(self, params: dict = None):
        p = {**DEFAULT_PARAMS, **(params or {})}

        # ── 관성 ──
        self.I         = p["I"]

        # ── 공기역학 ──
        self.rho       = p["rho"]
        self.S         = p["S"]
        self.l         = p["l"]
        self.Cl_alpha  = p["Cl_alpha"]
        self.Cd_roll   = p["Cd_roll"]

        # ── 서보 ──
        self.tau_servo     = p["tau_servo"]
        self.delta_max     = p["delta_max"]
        self.rate_limit    = p["rate_limit"]
        self.tau_servo_max = p["tau_servo_max"]

        # ── 상태 초기화 ──
        self.phi   = 0.0   # roll 각도 [rad]
        self.p     = 0.0   # roll 각속도 [rad/s]
        self.delta = 0.0   # 현재 핀 각도 [rad]

    # ─────────────────────────────────────
    # 공개 메서드
    # ─────────────────────────────────────

    def reset(self, phi0: float = 0.0, p0: float = 0.0):
        """시뮬레이션 초기 조건 설정"""
        self.phi   = phi0
        self.p     = p0
        self.delta = 0.0

    def step(self, delta_cmd: float, V: float, dt: float,
             disturbance: float = 0.0) -> tuple:
        """
        한 타임스텝 전진 (RK2 Midpoint method)

        Parameters
        ----------
        delta_cmd    : 제어기가 요청한 핀 각도 [rad]
        V            : 현재 비행 속도 [m/s]
        dt           : 타임스텝 [s]
        disturbance  : 외란 토크 [N·m]  (바람, 노이즈 등)

        Returns
        -------
        (phi, p) : 업데이트된 상태
        """
        # 1. 서보 모델 → 실제 핀 각도
        self.delta = self._servo_update(delta_cmd, dt)

        # 2. RK2 (Midpoint method)  I·dp/dt = tau,  dphi/dt = p
        # k1 — 현재 상태에서 기울기
        tau1  = self._compute_torque(V, self.p, self.delta, disturbance)
        dp1   = tau1 / self.I
        dphi1 = self.p

        # 중간점 상태 (dt/2 전진)
        p_mid   = self.p   + dp1   * (dt * 0.5)
        phi_mid = self.phi + dphi1 * (dt * 0.5)   # noqa: F841 (참고용)

        # k2 — 중간점에서 기울기 (delta, V는 동일)
        tau2  = self._compute_torque(V, p_mid, self.delta, disturbance)
        dp2   = tau2 / self.I
        dphi2 = p_mid

        # 최종 업데이트
        self.p   += dp2   * dt
        self.phi += dphi2 * dt

        return self.phi, self.p

    @property
    def state(self) -> tuple:
        """현재 상태 반환 (phi, p)"""
        return self.phi, self.p

    # ─────────────────────────────────────
    # 내부 메서드
    # ─────────────────────────────────────

    def _compute_torque(self, V: float, p: float,
                        delta: float, disturbance: float) -> float:
        """총 토크 계산 (RK2 중간점 계산에서도 재사용)"""
        return (self._aero_torque(V, delta)
                + self._roll_damping(V, p)
                + disturbance)

    def _Cl(self, delta: float) -> float:
        """
        양력 계수 (선형 모델)
        Cl = Cl_alpha * delta
        """
        return self.Cl_alpha * delta

    def _roll_damping(self, V: float, p: float) -> float:
        """
        Roll 댐핑 토크 (공기 마찰)
        tau_damp = -Cd_roll * 0.5 * rho * V² * S * l * p
                   (p에 비례하는 선형 댐핑 — 연속 미분 가능)

        속도가 빠를수록, 회전이 빠를수록 댐핑이 강해짐
        부호는 항상 p 반대 방향 (저항력)
        """
        q = 0.5 * self.rho * (V ** 2)   # 동압
        return -self.Cd_roll * q * self.S * self.l * p

    def _aero_torque(self, V: float, delta: float) -> float:
        """
        핀 공력 토크
        F   = 0.5 * rho * V^2 * Cl(delta) * S
        tau = F * l
        """
        F = 0.5 * self.rho * (V ** 2) * self._Cl(delta) * self.S
        return F * self.l

    def _servo_update(self, delta_cmd: float, dt: float) -> float:
        """
        서보 모델: 1차 지연 + rate limit + saturation

        1차 지연:   delta_dot = (delta_cmd - delta) / tau_servo
        rate limit: delta_dot 를 ±rate_limit 으로 클리핑
        saturation: delta     를 ±delta_max  로 클리핑
        """
        # 1차 지연
        delta_dot = (delta_cmd - self.delta) / self.tau_servo

        # Rate limit
        delta_dot = np.clip(delta_dot, -self.rate_limit, self.rate_limit)

        # 적분
        delta_new = self.delta + delta_dot * dt

        # Saturation (±delta_max)
        delta_new = np.clip(delta_new, -self.delta_max, self.delta_max)

        return delta_new


# ─────────────────────────────────────────
# 단독 실행 시 동작 확인용 테스트
# ─────────────────────────────────────────
if __name__ == "__main__":
    import matplotlib.pyplot as plt

    dt   = 0.01     # [s]
    T    = 5.0      # [s] 시뮬레이션 총 시간
    V    = 50.0     # [m/s] 일정 속도 가정

    rocket = RocketModel()
    rocket.reset(phi0=np.deg2rad(30))   # 초기 roll 30도

    time_arr = np.arange(0, T, dt)
    phi_log  = []
    p_log    = []

    # 제어기 없이 → 핀 고정(δ=0), 외란 없음
    # 관성만으로 계속 회전하는지 확인
    for t in time_arr:
        phi, p = rocket.step(delta_cmd=0.0, V=V, dt=dt)
        phi_log.append(np.rad2deg(phi))
        p_log.append(np.rad2deg(p))

    # 결과 출력
    fig, axes = plt.subplots(2, 1, figsize=(8, 5), sharex=True)

    axes[0].plot(time_arr, phi_log, color="#4a9eff")
    axes[0].set_ylabel("Roll angle φ [deg]")
    axes[0].set_title("물리 모델 테스트 (제어기 없음, 핀 고정)")
    axes[0].grid(True, alpha=0.3)
    axes[0].axhline(0, color='white', linewidth=0.5, linestyle='--')

    axes[1].plot(time_arr, p_log, color="#ff6b6b")
    axes[1].set_ylabel("Roll rate p [deg/s]")
    axes[1].set_xlabel("Time [s]")
    axes[1].grid(True, alpha=0.3)
    axes[1].axhline(0, color='white', linewidth=0.5, linestyle='--')

    plt.tight_layout()
    plt.savefig("/mnt/user-data/outputs/model_test.png", dpi=120,
                facecolor="#1a1a2e")
    print("✅ 저장 완료: model_test.png")
    print(f"   최종 φ = {phi_log[-1]:.2f} deg")
    print(f"   최종 p = {p_log[-1]:.2f} deg/s")
