"""
utils.py
--------
공통 유틸리티

- 한글 폰트 설정
- 그래프 스타일 공통 적용
"""

import matplotlib.pyplot as plt
import matplotlib.font_manager as fm


# ── Excel 표준 색상 팔레트 ──────────────────────────
XL_BLUE   = "#4472C4"
XL_ORANGE = "#ED7D31"
XL_GRAY   = "#A5A5A5"
XL_YELLOW = "#FFC000"
XL_LBLUE  = "#5B9BD5"
XL_GREEN  = "#70AD47"
XL_RED    = "#C00000"
XL_COLORS = [XL_BLUE, XL_ORANGE, XL_GREEN, XL_RED,
             XL_YELLOW, XL_LBLUE, XL_GRAY]


def set_korean_font():
    """
    한글 폰트 설정
    모든 그래프 저장 전에 한 번 호출하면 됨
    """
    candidates = [
        "Noto Sans CJK KR",
        "NanumGothic",
        "Malgun Gothic",
        "AppleGothic",
        "Noto Sans CJK JP",
    ]

    available = {f.name for f in fm.fontManager.ttflist}

    for font in candidates:
        if font in available:
            plt.rcParams["font.family"] = font
            plt.rcParams["axes.unicode_minus"] = False
            print(f"  ✅ 한글 폰트 설정: {font}")
            return font

    print("  ⚠️  한글 폰트 없음 → 영문으로 표시됩니다")
    plt.rcParams["axes.unicode_minus"] = False
    return None


def set_excel_style():
    """
    Excel 그래프 스타일 전역 적용
    set_korean_font() 다음에 호출하면 됨
    """
    plt.rcParams.update({
        # 배경
        "figure.facecolor"      : "white",
        "axes.facecolor"        : "white",

        # 텍스트/레이블
        "text.color"            : "#262626",
        "axes.labelcolor"       : "#262626",
        "axes.labelsize"        : 9,
        "axes.titlesize"        : 10,
        "axes.titleweight"      : "bold",
        "axes.titlecolor"       : "#262626",

        # 틱
        "xtick.color"           : "#595959",
        "ytick.color"           : "#595959",
        "xtick.labelsize"       : 8,
        "ytick.labelsize"       : 8,

        # 그리드
        "axes.grid"             : True,
        "grid.color"            : "#D9D9D9",
        "grid.linewidth"        : 0.8,
        "grid.alpha"            : 1.0,

        # 테두리
        "axes.spines.top"       : False,
        "axes.spines.right"     : False,
        "axes.edgecolor"        : "#BFBFBF",
        "axes.linewidth"        : 0.8,

        # 선
        "lines.linewidth"       : 1.8,

        # 범례
        "legend.framealpha"     : 1.0,
        "legend.edgecolor"      : "#BFBFBF",
        "legend.fontsize"       : 8,
        "legend.facecolor"      : "white",

        # 기본 색상 순서
        "axes.prop_cycle"       : plt.cycler(color=XL_COLORS),

        # 여백
        "figure.autolayout"     : False,
    })


def style_ax(ax, title=""):
    """단일 axes에 Excel 스타일 적용 (개별 조정용)"""
    ax.set_facecolor("white")
    ax.tick_params(colors="#595959", labelsize=8)
    ax.grid(True, color="#D9D9D9", linewidth=0.8)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    for sp in ["left", "bottom"]:
        ax.spines[sp].set_edgecolor("#BFBFBF")
        ax.spines[sp].set_linewidth(0.8)
    if title:
        ax.set_title(title, color="#262626", fontsize=10, fontweight="bold", pad=6)


def apply_dark_style(fig, axes):
    """다크 테마 공통 스타일 적용 (하위 호환)"""
    fig.patch.set_facecolor("#0d1117")
    for ax in (axes if hasattr(axes, '__iter__') else [axes]):
        ax.set_facecolor("#131a24")
        ax.tick_params(colors='#8899aa', labelsize=8)
        ax.grid(True, alpha=0.15, color='#334455')
        for spine in ax.spines.values():
            spine.set_edgecolor('#223344')
