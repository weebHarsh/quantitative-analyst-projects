import shutil
import subprocess
from pathlib import Path

import pytest

from pta import markouts, pnl
from pta.data.loaders import export_for_cpp
from pta.data.synthetic import MarketConfig, simulate_market
from pta.strategy.market_maker import MMConfig, simulate_market_maker

CPP = Path(__file__).resolve().parents[1] / "cpp" / "markout_kernel.cpp"
HORIZONS = (0, 100_000, 1_000_000, 5_000_000, 30_000_000)

pytestmark = pytest.mark.skipif(shutil.which("g++") is None, reason="g++ not available")


def parse(stdout: str):
    attr, mo = {}, {}
    for line in stdout.strip().splitlines():
        parts = line.split(",")
        if parts[0] == "markout":
            mo[int(parts[1])] = (int(parts[2]), float(parts[3]), float(parts[4]))
        else:
            attr[parts[0]] = float(parts[1])
    return attr, mo


def test_cpp_matches_python(tmp_path):
    market, flow = simulate_market(MarketConfig(seed=5, hours=1.0))
    fills = simulate_market_maker(market, flow, MMConfig(seed=9))
    assert len(fills) > 500

    exe = tmp_path / "markout_kernel"
    subprocess.run(["g++", "-O2", "-std=c++17", "-Wall", "-Wextra", str(CPP), "-o", str(exe)], check=True)
    fpath, mpath = export_for_cpp(fills, market, tmp_path)
    out = subprocess.run([str(exe), str(fpath), str(mpath), *map(str, HORIZONS)],
                         check=True, capture_output=True, text=True).stdout
    attr, cpp_mo = parse(out)

    _, totals = pnl.attribute(fills, market)
    assert attr["spread_capture"] == pytest.approx(totals["spread_capture"], rel=1e-9, abs=1e-9)
    assert attr["inventory"] == pytest.approx(totals["inventory"], rel=1e-9, abs=1e-9)
    assert attr["fees"] == pytest.approx(totals["fees"], rel=1e-9, abs=1e-9)
    assert attr["total"] == pytest.approx(totals["total"], rel=1e-9, abs=1e-9)

    summary = markouts.summarize(fills, markouts.compute(fills, market, HORIZONS)).set_index("horizon_us")
    for h in HORIZONS:
        n, qty, total = cpp_mo[h]
        assert n == summary.loc[h, "n"]
        assert qty == pytest.approx(summary.loc[h, "qty"])
        assert total == pytest.approx(summary.loc[h, "total"], rel=1e-9, abs=1e-9)
