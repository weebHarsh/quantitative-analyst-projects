import pandas as pd
import pytest

from pta.data.loaders import load_fills, market_from_quotes, validate


def test_ns_timestamps_use_exact_integer_division():
    base = 1_700_000_000_000_000_000
    df = pd.DataFrame({
        "t": [base + 999, base + 1000, base + 1999, base + 2000],
        "b": [1.0, 2.0, 3.0, 4.0],
        "a": [1.1, 2.1, 3.1, 4.1],
    })
    m = market_from_quotes(df, "t", "b", "a", ts_unit="ns")
    # 999ns -> us 0, 1000ns and 1999ns -> us 1 (last quote wins), 2000ns -> us 2
    assert m["ts_us"].tolist() == [base // 1000, base // 1000 + 1, base // 1000 + 2]
    assert m["bid"].tolist() == [1.0, 3.0, 4.0]


def test_ms_and_us_units():
    df = pd.DataFrame({"t": [1_700_000_000_123, 1_700_000_000_124], "b": [1.0, 2.0], "a": [1.5, 2.5]})
    assert market_from_quotes(df, "t", "b", "a", "ms")["ts_us"].tolist() == [1_700_000_000_123_000,
                                                                              1_700_000_000_124_000]
    assert market_from_quotes(df, "t", "b", "a", "us")["ts_us"].tolist() == [1_700_000_000_123,
                                                                              1_700_000_000_124]


def test_fractional_quantity_is_preserved(tmp_path):
    p = tmp_path / "fills.csv"
    p.write_text("ts_us,side,px,qty,fee\n0,1,100,1.5,0\n1,-1,100,0.5,0\n")
    fills = load_fills(p)
    assert fills["qty"].tolist() == [1.5, 0.5]
    market = pd.DataFrame({"ts_us": [0, 1, 2], "bid": 1.0, "ask": 1.0, "mid": [100.0, 100.0, 100.0]})
    validate(fills, market)


def test_validate_rejects_nonpositive_quantity():
    market = pd.DataFrame({"ts_us": [0, 1], "bid": 1.0, "ask": 1.0, "mid": [1.0, 1.0]})
    fills = pd.DataFrame({"ts_us": [0], "side": [1], "px": [1.0], "qty": [0.0], "fee": [0.0]})
    with pytest.raises(ValueError):
        validate(fills, market)
