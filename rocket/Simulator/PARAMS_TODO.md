# 실측 필요 파라미터 정리
> 이 파일의 값들을 실측 후 해당 위치에 반영하세요.

---

## 1. 로켓 구조 파라미터
**파일: `simulator.py` (상단 상수)**

| 파라미터 | 현재값 | 단위 | 측정 방법 | 반영 위치 |
|----------|--------|------|-----------|-----------|
| `ROCKET_MASS_TOTAL` | 6.0 | kg | 발사 직전 저울 측정 (연료 포함) | simulator.py:46 |
| `FUEL_MASS` | 1.5 | kg | 연소 전후 무게 차이 | simulator.py:47 |
| `ROCKET_RADIUS` | 0.05 | m | 동체 외경 / 2 | simulator.py:49 |

```python
# simulator.py 반영 예시
ROCKET_MASS_TOTAL = [실측값]   # kg
FUEL_MASS         = [실측값]   # kg
ROCKET_RADIUS     = [실측값]   # m
```

---

## 2. 관성 모멘트 (Roll)
**파일: `rocket_model.py` → `DEFAULT_PARAMS["I"]`**

| 파라미터 | 현재값 | 단위 | 측정 방법 | 반영 위치 |
|----------|--------|------|-----------|-----------|
| `I` (roll 관성모멘트) | 0.0075 | kg·m² | 비파괴 측정법 또는 CAD 계산 | rocket_model.py:29 |

**측정 방법 옵션:**
- **트라이피럴법**: 실 3개로 매달아 진동 주기 측정 → `I = m × g × r² × T² / (4π² × L)`
- **CAD**: 설계 소프트웨어에서 직접 추출
- **근사식**: 원기둥 가정 `I = 0.5 × m × r²` (현재 사용 중)

```python
# rocket_model.py 반영
"I" : [실측값],   # kg·m²
```

---

## 3. 공기역학 파라미터
**파일: `rocket_model.py` → `DEFAULT_PARAMS`**

| 파라미터 | 현재값 | 단위 | 측정/계산 방법 | 반영 위치 |
|----------|--------|------|----------------|-----------|
| `S` (핀 면적 × 2) | 0.0008 | m² | 핀 실제 치수 측정 후 계산: `폭 × 높이 × 2` | rocket_model.py:33 |
| `l` (핀~중심축 거리) | 0.03 | m | 핀 압력중심 ~ 동체 중심축 직접 측정 | rocket_model.py:34 |
| `Cl_alpha` (양력 기울기) | 1.141 | 1/rad | OpenRocket / XFLR5 / 풍동 실험 | rocket_model.py:35 |
| `Cd_roll` (roll 댐핑) | 0.02 | - | 자유 스핀 실험 (감쇠 측정) | rocket_model.py:36 |

```python
# rocket_model.py 반영
"S"        : [핀폭 × 핀높이 × 2],   # m²
"l"        : [실측값],               # m
"Cl_alpha" : [계산값],               # 1/rad  (OpenRocket 권장)
"Cd_roll"  : [실측값],               # -
```

**Cl_alpha 간이 계산식 (얇은 날개 이론):**
```
AR = 날개 가로세로비 = span² / 면적
Cl_alpha = 2π × AR / (AR + 2)   [1/rad]
```

---

## 4. 항력 계수
**파일: `simulator.py`**

| 파라미터 | 현재값 | 단위 | 측정 방법 | 반영 위치 |
|----------|--------|------|-----------|-----------|
| `CD` (전체 항력계수) | 0.5 | - | OpenRocket 시뮬 또는 시험발사 속도 역산 | simulator.py:55 |

```python
# simulator.py 반영
CD = [실측값]   # -
```

---

## 5. 서보 파라미터
**파일: `rocket_model.py` → `DEFAULT_PARAMS`**

| 파라미터 | 현재값 | 단위 | 측정 방법 | 반영 위치 |
|----------|--------|------|-----------|-----------|
| `tau_servo` (응답 시상수) | 0.03 | s | 스텝 입력 후 응답 시간 측정: 63% 도달 시간 | rocket_model.py:39 |
| `delta_max` (최대 편향각) | 15° | deg | 서보 + 링크 조립 후 실제 최대각 측정 | rocket_model.py:40 |
| `rate_limit` (최대 각속도) | 11.64 | rad/s | 데이터시트: `(π/3) / transit_time` | rocket_model.py:41 |
| `tau_servo_max` (최대 토크) | 4.898 | N·m | 데이터시트 (현재: 34.70 oz-in × 2개) | rocket_model.py:42 |

```python
# rocket_model.py 반영
"tau_servo"     : [실측값],            # s  (스텝 응답 측정)
"delta_max"     : np.deg2rad([실측값]),# rad
"rate_limit"    : np.deg2rad([°/s]),   # rad/s
"tau_servo_max" : [토크_N·m × 2],     # N·m (2개)
```

---

## 6. 추력 곡선 (시험발사 데이터)
**파일: `simulator.py` → `THRUST_TIME`, `THRUST_FORCE`**

현재 1차 추력 실험 데이터 사용 중. 새 시험발사 후 교체 필요.

```python
# simulator.py 반영
THRUST_TIME = np.array([
    # 시험발사 측정 시간 [s] — 0.08s 간격 권장
    0, 0.08, 0.17, ...
])

THRUST_FORCE = np.array([
    # 각 시간의 추력 [N]
    0.0, ..., 최대추력, ..., 0.0
])
```

**측정 데이터 포맷:**
- 로드셀 샘플링: 최소 100Hz 권장
- 시작: 점화 직전 0값 포함
- 끝: 추력 < 1N 이후까지 기록

---

## 7. 제어기 기준 속도
**파일: `controller.py`**

| 파라미터 | 현재값 | 단위 | 기준 | 반영 위치 |
|----------|--------|------|------|-----------|
| `V_REF` | 20.0 | m/s | 관성비행 구간 중간 속도 | controller.py:30 |

시험발사 후 속도 프로파일 확인해서 **관성비행 평균 속도**로 수정:

```python
# controller.py 반영
V_REF = [시험발사_관성비행_평균속도]   # m/s
```

---

## 8. Monte Carlo 외란 파라미터
**파일: `monte_carlo.py` → `default_dist`**

실제 측정 후 현실적인 값으로 수정 권장:

| 파라미터 | 현재값 | 단위 | 기준 |
|----------|--------|------|------|
| `wind_std` | 0.0001 | N·m | 바람 토크 표준편차 → 발사장 풍속 기반 조정 |
| `mass_var` | 0.03 | ratio | 질량 변동 ±3% |
| `servo_delay` | 0.002 | s | 서보 추가 지연 ±2ms |

---

## 측정 우선순위

```
[필수 — 시뮬 정확도에 직접 영향]
  1. ROCKET_MASS_TOTAL, FUEL_MASS   ← 저울
  2. THRUST_TIME, THRUST_FORCE      ← 시험발사 로드셀
  3. S, l (핀 치수)                 ← 직접 측정
  4. tau_servo, delta_max           ← 서보 실측

[중요 — 가능하면 측정]
  5. I (관성모멘트)                 ← 트라이피럴 or CAD
  6. Cl_alpha                       ← OpenRocket
  7. CD                             ← OpenRocket or 역산

[낮음 — 현재값 사용 가능]
  8. Cd_roll                        ← 자유 스핀 실험
  9. V_REF                          ← 시험발사 후 속도 확인
```

---

## 반영 후 재최적화 순서

```
1. 위 값들 측정 및 반영
2. main.py 실행 → 새 Kp_base, Kd_base 최적화
3. controller.py의 KP_BASE, KD_BASE 업데이트
4. 아두이노 코드에 반영
```
