#!/usr/bin/env python3
"""Offline LG battery/profile audit; reads source and SWEETLOG files only.

This reproduces the inspected LG integer formula, not a generic C/DTS
interpreter. A candidate never grants charging permission. No device I/O.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import struct


def require(condition, message):
    if not condition:
        raise ValueError(message)


def source(root, relative, evidence):
    data = (root / relative).read_bytes()
    evidence[relative] = hashlib.sha256(data).hexdigest()
    return data.decode()


def scalar(text, name):
    match = re.search(re.escape(name) + r"\s*=\s*<\s*(\d+)\s*>", text)
    require(match is not None, "missing scalar " + name)
    return int(match[1])


def persistent(path):
    data = path.read_bytes()
    require(len(data) >= 4096, "short SWEETLOG header")
    magic, version, size, total, seq = struct.unpack_from("<8sIIQQ", data)
    require(magic == b"SWEETLOG" and version == 1, "unsupported SWEETLOG")
    require(size > 0 and len(data) >= 4096 + size, "truncated SWEETLOG ring")
    ring = data[4096:4096 + size]
    start = total % size
    body = ring[:total] if total <= size else ring[start:] + ring[:start]
    require(b"\0" not in body, "NUL in valid SWEETLOG body")
    return body.decode("utf-8", "replace").replace("\r", ""), {
        "name": path.name, "sha256": hashlib.sha256(data).hexdigest(),
        "version": version, "ring_bytes": size, "total_bytes": total,
        "flush_sequence": seq, "wrapped": total > size,
    }


def values_summary(values):
    require(bool(values), "missing accepted ADC samples")
    return {"count": len(values), "minimum": min(values),
            "median": statistics.median(values), "maximum": max(values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lg-source", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--input-log", action="append", default=[], type=Path,
                        help="older preflight log, historical evidence only")
    args = parser.parse_args()
    evidence = {}
    supply = "drivers/power/supply/"
    extension = source(args.lg_source, supply + "lge/extension-fg-gen4.c", evidence)
    # Reject changed source semantics instead of silently applying this snapshot.
    require(re.search(r"#define\s+BIAS_MVOLTAGE\s+1825\b", extension),
            "LG bias changed; review formula before reusing this tool")
    compact = re.sub(r"\s+", "", extension)
    require("(100*1000*1000)/(BIAS_MVOLTAGE*1000/(batt_id_adc/1000)-1000)"
            in compact, "LG integer formula changed")
    require("fg->bp.fastchg_curr_ma=dt_fcc;" in compact,
            "LG supplement FCC override changed")
    fg = source(args.lg_source, supply + "qcom/qpnp-fg-gen4.c", evidence)
    for name, value in (("PROFILE_LOAD_WORD", 65), ("PROFILE_LOAD_OFFSET", 0),
                        ("PROFILE_COMP_LEN", 24), ("NOM_CAP_WORD", 271),
                        ("NOM_CAP_OFFSET", 0)):
        require(re.search(r"#define\s+" + name + r"\s+" + str(value) + r"\b", fg),
                "FG constant changed: " + name)
    dt = "arch/arm64/boot/dts/lge/"
    board = source(args.lg_source, dt + "sm8150-mh2lm/sm8150-mh2lm_common/"
                   "sm8150-mh2lm_common-pm.dtsi", evidence)
    tolerance = scalar(board, "qcom,batt-id-range-pct")
    supplement = scalar(board, "qcom,fastchg-current-ma")
    text, logmeta = persistent(args.log)
    accepted = re.findall(
        r"SWEET BATT ADC: sample channel=(\d+) token=\d+ epoch=\d+ uv=(\d+) valid=1\b",
        text)
    batid = [int(uv) for ch, uv in accepted if ch == "1"]
    vbus = [int(uv) for ch, uv in accepted if ch == "2"]
    require(bool(batid) and bool(vbus), "missing accepted BAT_ID/USBIN samples")

    def resistance(uv):
        # Keep LG's integer order: truncate to mV BEFORE dividing the bias.
        mv = uv // 1000
        require(0 < mv < 1825, "BAT_ID outside LG divider domain")
        denominator = 1825000 // mv - 1000
        require(denominator > 0, "invalid LG divider denominator")
        return 100000000 // denominator

    ohms = [resistance(uv) for uv in batid]
    kohms = [value // 1000 for value in ohms]
    capacity = re.findall(
        r"SWEET BATT CAP: v3 id=\d+ nominal=(\d+) n-ok=1 n-error=0 .*?"
        r"profile=(0x[0-9a-fA-F]+|\d+) p-ok=1 p-error=0", text)
    require(bool(capacity), "missing valid FG capacity/profile records")
    profiles = []
    for vendor in ("Tocad", "LGC"):
        filename = "LGE_BLT42_" + vendor + "_4000mAh.dtsi"
        require(filename in board, "profile absent from target board include")
        profile = source(args.lg_source, dt + "battery_profile/" + filename, evidence)
        match = re.search(r"qcom,fg-profile-data\s*=\s*\[([^]]+)\]", profile)
        require(match is not None, "missing profile data")
        blob = bytes.fromhex(match[1])
        require(len(blob) == 416, "unexpected profile length")
        nominal_id = scalar(profile, "qcom,batt-id-kohm")
        limit = nominal_id * tolerance // 100
        nominal_mah = int.from_bytes(blob[(271 - 65) * 2:(271 - 65) * 2 + 2], "little")
        id_all = all(abs(value - nominal_id) <= limit for value in kohms)
        cap_all = all(int(value) == nominal_mah for value, _ in capacity)
        profiles.append({
            "name": filename.removesuffix(".dtsi"), "id_kohm": nominal_id,
            "integer_id_range_kohm": [nominal_id - limit, nominal_id + limit],
            "fv_uv": scalar(profile, "qcom,max-voltage-uv"),
            "profile_fcc_ma": scalar(profile, "qcom,fastchg-current-ma"),
            "board_supplement_fcc_ma": supplement,
            "nominal_mah_in_profile": nominal_mah,
            "prefix24_hex": blob[:24].hex(),
            "profile_sha256": hashlib.sha256(blob).hexdigest(),
            "all_measured_ids_match": id_all,
            "all_observed_nominal_capacities_match": cap_all,
            "consistent_candidate": id_all and cap_all,
            "loaded_prefix_measured": False,
        })
    historical = []
    for path in args.input_log:
        oldtext, metadata = persistent(path)
        fields = re.findall(r"SWEET CHARGER PREFLIGHT: sid=2 addr=(0x[0-9a-f]+) "
                            r"apid=\d+ write-ee=\d+ irq-owner=\d+ raw=(0x[0-9a-f]+|\d+) "
                            r"at-ns=(\d+)", oldtext)
        historical.append({"log": metadata, "current_input_permission": False,
                           "preflight": [{"address": address, "raw": int(raw, 0),
                                          "at_ns": int(ns)} for address, raw, ns in fields]})
    report = {
        "scope": "offline candidate audit; no charging authorization",
        "log": logmeta, "lg_source_sha256": evidence,
        "lg_bat_id_bias_mv": 1825, "lg_bat_id_pullup_ohms": 100000,
        "accepted_bat_id_uv": values_summary(batid),
        "lg_integer_resistance_ohms": values_summary(ohms),
        "lg_integer_id_kohm": sorted(set(kohms)),
        "accepted_vbus_uv": values_summary(vbus),
        "observed_nominal_mah": sorted({int(v) for v, _ in capacity}),
        "observed_integrity": sorted({int(v, 0) for _, v in capacity}),
        "profiles": profiles, "historical_input_evidence": historical,
        "unresolved": ["loaded FG profile prefix", "current input capability and role",
                       "parallel path isolation", "watchdog maintenance"],
        "charging_authorized": False,
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit("battery evidence audit: " + str(error)) from error
