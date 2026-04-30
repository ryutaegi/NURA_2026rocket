"""
controller.py
-------------
PD 제어기 + 동압 정규화 (Dynamic Pressure Normalization)

제어 법칙:
    q      = 0.5 × rho × V²           현재 동압
    q_ref  = 0.5 × rho × V_ref²       기준 동압 (V=20 m/s)
    scale  = q_ref / q                 스케일 팩터 (1/V²에 비례)

    Kp_eff = Kp_base × scale          유효 Kp (저속일수록 자동으로 커짐)
    Kd_eff = Kd_base × scale          유효 Kd

    u = Kp_eff × (0 - phi) - Kd_eff × p

장점:
    - 구간 나누기 불필요 → 완전 연속적
    - BO가 Kp_base, Kd_base 2개만 탐색 (기존 10개 → 2개)
    - 물리 법칙 기반 → 저속/고속 모두 일관된 제어력
    - 구간 경계에서 불연속 없음
"""

import numpy as np


# ─────────────────────────────────────────
# 동압 정규화 파라미터
# ─────────────────────────────────────────
RHO       = 1.225    # [kg/m³]  공기밀도
V_REF     = 20.0     # [m/s]   기준 속도 (관성 비행 중간 속도)
Q_REF     = 0.5 * RHO * V_REF ** 2   # = 245 Pa  기준 동압
Q_MIN     = 0.5 * RHO * 2.0  ** 2    # 최소 동압 (V=2 m/s, 발산 방지)

# 기본 gain (BO 최적화 전 초기값)
KP_BASE   = 3.6495   # 기준 속도(V=20)에서의 Kp
KD_BASE   = 2.0165    # 기준 속도(V=20)에서의 Kd


class PDController:
    """
    PD 제어기 (동압 정규화)

    사용 예:
        ctrl = PDController(Kp_base=1.5, Kd_base=0.8)
        u = ctrl.update(phi=current_phi, p=current_p, V=current_speed)
    """

    def __init__(self,
                 Kp_base: float = KP_BASE,
                 Kd_base: float = KD_BASE,
                 phi_target: float = 0.0,
                 # 하위 호환성 유지 (기존 코드에서 Kp, Kd로 호출 시)
                 Kp: float = None,
                 Kd: float = None,
                 use_scheduling: bool = True,
                 gain_schedule: list = None):
        """
        Parameters
        ----------
        Kp_base    : V=20 m/s 기준 비례 gain
        Kd_base    : V=20 m/s 기준 미분 gain
        phi_target : 목표 roll 각도 [rad]
        """
        self.phi_target = phi_target

        # 하위 호환성: 기존 Kp, Kd 인자도 받음
        self.Kp_base = Kp if Kp is not None else Kp_base
        self.Kd_base = Kd if Kd is not None else Kd_base

        # 로그용
        self.last_Kp  = self.Kp_base
        self.last_Kd  = self.Kd_base
        self.last_err = 0.0
        self.last_u   = 0.0

    # ─────────────────────────────────────
    # 공개 메서드
    # ─────────────────────────────────────

    def update(self, phi: float, p: float, V: float = V_REF) -> float:
        """
        제어 명령 계산

        Parameters
        ----------
        phi : 현재 roll 각도 [rad]
        p   : 현재 roll 각속도 [rad/s]
        V   : 현재 비행 속도 [m/s]

        Returns
        -------
        u   : 핀 명령 각도 [rad]
        """
        # 동압 계산
        q     = max(0.5 * RHO * V ** 2, Q_MIN)   # 발산 방지
        scale = Q_REF / q                          # 1/V²에 비례

        # 유효 gain (동압에 반비례)
        Kp_eff = self.Kp_base * scale
        Kd_eff = self.Kd_base * scale

        # PD 제어
        error = self.phi_target - phi
        u     = Kp_eff * error - Kd_eff * p

        # 핀 포화 방지 (±15도)
        u = np.clip(u, -np.deg2rad(15), np.deg2rad(15))

        # 로그
        self.last_Kp  = Kp_eff
        self.last_Kd  = Kd_eff
        self.last_err = error
        self.last_u   = u

        return u

    def set_target(self, phi_target: float):
        """목표 roll 각도 변경 [rad]"""
        self.phi_target = phi_target

    def set_gains(self, Kp_base: float, Kd_base: float):
        """기준 gain 변경"""
        self.Kp_base = Kp_base
        self.Kd_base = Kd_base

    @property
    def status(self) -> dict:
        return {
            "Kp_eff" : self.last_Kp,
            "Kd_eff" : self.last_Kd,
            "error"  : np.rad2deg(self.last_err),
            "u"      : np.rad2deg(self.last_u),
        }


# ─────────────────────────────────────────
# 단독 실행 테스트
# ─────────────────────────────────────────
if __name__ == "__main__":
    import sys, os
    import matplotlib.pyplot as plt
    sys.path.insert(0, os.path.dirname(__file__))
    from rocket_model import RocketModel

    dt = 0.01; T = 10.0

    rocket = RocketModel()
    rocket.reset(phi0=np.deg2rad(30))
    ctrl = PDController(Kp_base=KP_BASE, Kd_base=KD_BASE)

    from simulator import velocity_profile, THRUST_TIME
    BURN_CUTOFF = THRUST_TIME[-1] - 1.5

    time_arr = np.arange(0, T, dt)
    phi_log, p_log, u_log, kp_log = [], [], [], []

    for t in time_arr:
        phi, p = rocket.state
        V = velocity_profile(t)
        u = 0.0 if t < BURN_CUTOFF else ctrl.update(phi=phi, p=p, V=V)
        rocket.step(delta_cmd=u, V=V, dt=dt)
        phi_log.append(np.rad2deg(phi))
        p_log.append(np.rad2deg(p))
        u_log.append(np.rad2deg(u))
        kp_log.append(ctrl.last_Kp)

    fig, axes = plt.subplots(4, 1, figsize=(9, 9), sharex=True)
    fig.patch.set_facecolor("#1a1a2e")
    colors = ["#4a9eff", "#ff6b6b", "#06d6a0", "#ffd166"]
    labels = ["Roll angle φ [deg]", "Roll rate p [deg/s]",
              "Fin cmd δ [deg]", "Kp_eff"]
    data   = [phi_log, p_log, u_log, kp_log]

    for ax, d, color, label in zip(axes, data, colors, labels):
        ax.set_facecolor("#0d1117")
        ax.plot(time_arr, d, color=color, linewidth=1.8)
        ax.set_ylabel(label, color='#aaaaaa', fontsize=9)
        ax.tick_params(colors='#aaaaaa')
        ax.grid(True, alpha=0.2)
        ax.axhline(0, color='white', linewidth=0.5, linestyle='--')
        for spine in ax.spines.values():
            spine.set_edgecolor('#333')

    axes[0].set_title(
        f"동압 정규화 PD 제어기  |  Kp_base={KP_BASE}, Kd_base={KD_BASE}",
        color='white', fontsize=11)
    axes[-1].set_xlabel("Time [s]", color='#aaaaaa')

    plt.tight_layout()
    plt.savefig("/mnt/user-data/outputs/controller_test.png",
                dpi=120, facecolor="#1a1a2e")

    print(f"최종 φ     = {phi_log[-1]:.3f} deg")
    print(f"최종 p     = {p_log[-1]:.3f} deg/s")
    print(f"안정화 여부 = {'✅ 안정' if abs(phi_log[-1]) < 15 else '❌ 불안정'}")
