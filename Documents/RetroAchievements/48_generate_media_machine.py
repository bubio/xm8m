#!/usr/bin/env python3
"""Validate the media model and generate Markdown and the C++ transition table."""

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MODEL = ROOT / "48_media_machine.json"
OUTPUT = ROOT / "48_RA媒体状態機械遷移表.generated.md"
CPP_OUTPUT = ROOT.parent.parent / "Source/RA/ra_media_operation_table.generated.h"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(model):
    require(model["schema_version"] == 1, "unsupported schema")
    states, events, effects = model["states"], model["events"], model["effects"]
    require("Idle" in states, "missing Idle")
    for event, values in events.items():
        require(values and len(set(values)) == len(values), f"bad domain: {event}")
    rows = model["transitions"]
    by_id, cells = {}, defaultdict(list)
    for row in rows:
        rid = row["id"]
        require(rid not in by_id, f"duplicate ID: {rid}")
        by_id[rid] = row
        require(row["source"] in states and row["target"] in states, f"bad state: {rid}")
        require(row["event"] in events, f"bad event: {rid}")
        require(row["view"] in model["views"], f"bad view: {rid}")
        require(row["basis"], f"missing basis: {rid}")
        require(row["values"], f"empty values: {rid}")
        require(all(v in events[row["event"]] for v in row["values"]), f"bad value: {rid}")
        require(all(e in effects for e in row["effects"]), f"bad effect: {rid}")
        cells[row["source"], row["event"]].append(row)
    for (state, event), alternatives in cells.items():
        counts = Counter(v for row in alternatives for v in row["values"])
        require(counts == Counter(events[event]), f"missing/overlapping branches: {state}/{event}")

    producers = model["effect_results"]
    require(set(producers) <= set(effects), "unknown producing effect")
    require(set(producers.values()) <= set(events), "unknown produced event")
    for row in rows:
        produced = [producers[e] for e in row["effects"] if e in producers]
        expected = {event for state, event in cells if state == row["target"]}
        if row["target"] == "Idle":
            require(not produced, f"completion leaves a pending effect: {row['id']}")
        else:
            require(len(produced) == 1 and set(produced) == expected,
                    f"effect result does not match target wait: {row['id']}")

    # Expand every cell. Generic classifications may not hide a normal transition.
    matrix = {}
    busy = model["busy"]
    require(busy["event"] in events, "invalid busy event")
    require(set(busy["except_states"]) <= set(states), "invalid busy exception")
    require(model["unexpected"]["code"] == "X", "unexpected cells must detect errors")
    generic = model["generic_cells"]
    require(set(generic) <= set(events), "unknown generic event")
    for policy in [*generic.values(), busy, model["unexpected"]]:
        require(policy["effect"] in effects, "unknown generic effect")
    for state in states:
        for event in events:
            normal = cells.get((state, event))
            is_busy = event == busy["event"] and state not in busy["except_states"]
            require(sum((bool(normal), event in generic, is_busy)) <= 1,
                    f"overlapping cell policies: {state}/{event}")
            if normal:
                matrix[state, event] = ", ".join(row["id"] for row in normal)
            elif event in generic:
                matrix[state, event] = generic[event]["code"]
            elif is_busy:
                matrix[state, event] = busy["code"]
            else:
                matrix[state, event] = model["unexpected"]["code"]

    def reachable(start, reverse=False):
        found = {start}
        while True:
            old = len(found)
            for row in rows:
                a, b = row["source"], row["target"]
                if reverse:
                    a, b = b, a
                if a in found:
                    found.add(b)
            if len(found) == old:
                return found

    require(reachable("Idle") == set(states), "unreachable state")
    require(reachable("Idle", reverse=True) == set(states), "state has no path to Idle")
    scenario_ids = set()
    for case in model["scenarios"]:
        cid = case["id"]
        require(cid not in scenario_ids, f"duplicate scenario: {cid}")
        scenario_ids.add(cid)
        state, counts, reset_kind = "Idle", Counter(), None
        choices = case.get("choices", {})
        require(set(choices) <= set(case["transitions"]), f"unused choices: {cid}")
        for rid in case["transitions"]:
            require(rid in by_id, f"unknown transition: {cid}/{rid}")
            row = by_id[rid]
            require(state == row["source"], f"disconnected trace: {cid}/{rid}")
            require(len(row["values"]) == 1 or rid in choices, f"ambiguous trace: {cid}/{rid}")
            value = choices.get(rid, row["values"][0])
            require(value in row["values"], f"bad trace value: {cid}/{rid}")
            if row["event"] == "FinishPlan":
                reset_kind = value
            elif row["event"] == "ResetDone":
                allowed = {"reanchor", "noanchor"} if reset_kind == "reanchor" else {reset_kind}
                require(value in allowed, f"reset disposition mismatch: {cid}/{rid}")
            counts.update(row["effects"])
            state = row["target"]
        require(state == "Idle", f"unfinished trace: {cid}")
        for effect, count in case["effect_counts"].items():
            require(effect in effects, f"unknown expected effect: {cid}/{effect}")
            require(counts[effect] == count, f"effect count mismatch: {cid}/{effect}")
        require(sum(counts[e] for e in ("RejectLocal", "CompleteFailure", "CompleteSuccess")) == 1,
                f"completion count is not one: {cid}")
    return matrix, by_id


def render(model, matrix, by_id):
    lines = ["# RA媒体状態機械 遷移表・図（生成物）", "",
             "**設計モデル。本番実装の検証結果ではない。手編集禁止。**", "",
             "意味・適用範囲・判断関数は[48詳細設計](48_RA媒体状態機械詳細設計.md)を参照。",
             "定義は[JSON](48_media_machine.json)、生成は[Python](48_generate_media_machine.py)。", "",
             "## 1. 全二次元表", "",
             "行はoperation、列はevent。Tは詳細遷移、BはBusy、Nは通信更新、Sは古い結果の破棄、Xは定義違反。",
             "Submit前の受付判定と独立Ejectは48 §4.2に従う。Xを正常経路の網羅件数に含めない。", ""]
    events = list(model["events"])
    for offset in range(0, len(events), 6):
        batch = events[offset:offset + 6]
        lines += ["| operation | " + " | ".join(batch) + " |",
                  "|---|" + "---|" * len(batch)]
        for state in model["states"]:
            lines.append("| " + state + " | " + " | ".join(matrix[state, e] for e in batch) + " |")
        lines.append("")
    lines += ["## 2. 状態とイベントの有限値", "", "| 状態 | 意味 |", "|---|---|"]
    lines += [f"| {key} | {value} |" for key, value in model["states"].items()]
    lines += ["", "| イベント | 分岐値（相互排他） |", "|---|---|"]
    lines += [f"| {key} | {', '.join(value)} |" for key, value in model["events"].items()]
    lines += ["", "rejectedは別Game ID／未登録、unavailableは開始不能・認証不能・照会不能等。",
              "これらのRA結果をローカル準備失敗やVM適用失敗へ変換しない。", "",
              "## 3. 詳細遷移", "",
              "副作用は左から順に実行する。状態確定後に発行し、結果は別イベントとして処理する。", "",
              "| ID | 現状態 | イベント / 分岐値 | 次状態 | 順序付き副作用 | 根拠 |", "|---|---|---|---|---|---|"]
    for row in model["transitions"]:
        lines.append(f"| {row['id']} | {row['source']} | {row['event']} / {', '.join(row['values'])} | "
                     f"{row['target']} | {' → '.join(row['effects'])} | {row['basis']} |")
    lines += ["", "## 4. Mermaid図", "",
              "全T遷移を三つの表示に分ける。同名状態は同一であり、別の状態機械ではない。",
              "B/N/S/Xの共通セルは二次元表を参照。エッジはT番号・イベント・分岐値を示し、副作用は詳細遷移表へ対応する。", ""]
    for view, title in model["views"].items():
        rows = [row for row in model["transitions"] if row["view"] == view]
        nodes = {row[key] for row in rows for key in ("source", "target")}
        lines += [f"### {title}", "", "```mermaid", "stateDiagram-v2", "    direction TB"]
        for state, label in model["states"].items():
            if state in nodes:
                lines.append(f'    state "{label} ({state})" as {state}')
        edges = defaultdict(list)
        for row in rows:
            label = f"{row['id']} {row['event']}={','.join(row['values'])}"
            edges[row["source"], row["target"]].append(label)
        for (source, target), ids in edges.items():
            lines.append(f"    {source} --> {target}: {' / '.join(ids)}")
        lines += ["```", ""]
    lines += ["## 5. 副作用の契約", "", "| 副作用 | 契約 |", "|---|---|"]
    lines += [f"| {key} | {value} |" for key, value in model["effects"].items()]
    lines += ["", "| 結果を生成する副作用 | 配送するイベント |", "|---|---|"]
    lines += [f"| {key} | {value} |" for key, value in model["effect_results"].items()]
    lines += ["", "## 6. 抽象経路の検証例", "",
              "イベント列の接続と列挙したeffect回数を検証する。入力変換・判断の正しさ・実Drive結果は本番試験で確認する。", ""]
    for case in model["scenarios"]:
        lines += [f"### {case['id']} {case['title']}", ""]
        trace = []
        for rid in case["transitions"]:
            row = by_id[rid]
            value = case.get("choices", {}).get(rid, row["values"][0])
            trace.append(f"{rid}({value})")
        lines += [" → ".join(trace), "",
                  "観測: " + ", ".join(f"{e}={n}" for e, n in case["effect_counts"].items()), ""]
    lines += ["## 7. 機械検証の集計", ""]
    counts = Counter("T" if v.startswith("T") else v for v in matrix.values())
    lines += [f"- 状態: {len(model['states'])}、イベント: {len(events)}、全セル: {len(matrix)}",
              f"- 通常セル: {counts['T']}、T遷移: {len(model['transitions'])}",
              f"- B: {counts['B']}、N: {counts['N']}、S: {counts['S']}、X: {counts['X']}",
              f"- 抽象経路: {len(model['scenarios'])}", "",
              "有限分岐の全値が一意に遷移することと、各状態からIdleへの経路の存在を検証する。",
              "これは外部応答の到着や本番の全状態到達性の証明ではない。", ""]
    return "\n".join(lines)


def render_cpp(model, matrix):
    states, events, effects = list(model["states"]), list(model["events"]), list(model["effects"])
    values = list(dict.fromkeys(v for domain in model["events"].values() for v in domain))
    require(len(values) <= 64, "C++ value mask exceeds 64 bits")
    require(max(len(t["effects"]) for t in model["transitions"]) <= 4, "C++ effect capacity exceeded")
    def mask(domain):
        return "0x%xULL" % sum(1 << values.index(v) for v in domain)
    lines = ["// Generated from Documents/RetroAchievements/48_media_machine.json. Do not edit.",
             "#ifndef XM8_RA_MEDIA_OPERATION_TABLE_GENERATED_H",
             "#define XM8_RA_MEDIA_OPERATION_TABLE_GENERATED_H", "",
             "#include <array>", "#include <cstddef>", "#include <cstdint>", "",
             "namespace Xm8Ra { namespace MediaOperation {", ""]
    for name, items in [("State", states), ("Event", events), ("Value", values), ("Effect", effects)]:
        lines += ["enum class " + name + " { " + ", ".join(items + ["Count"]) + " };"]
    lines += ["enum class CellKind { Normal, Busy, Connectivity, Stale, Invalid };", "",
              "struct Transition {", "    std::uint64_t values;", "    State next;",
              "    std::array<Effect, 4> effects;", "    std::size_t effect_count;",
              "    unsigned id;", "};", "struct Cell {", "    CellKind kind;",
              "    std::size_t first;", "    std::size_t count;", "    Effect generic_effect;", "};", "",
              "static constexpr Transition kTransitions[] = {"]
    cells = {}
    offset = 0
    kinds = {"B": "Busy", "N": "Connectivity", "S": "Stale", "X": "Invalid"}
    policies = {p["code"]: p["effect"] for p in [*model["generic_cells"].values(), model["busy"], model["unexpected"]]}
    for state in states:
        for event in events:
            rows = [t for t in model["transitions"] if t["source"] == state and t["event"] == event]
            if rows:
                cells[state, event] = "{CellKind::Normal, %d, %d, Effect::ProtocolError}" % (offset, len(rows))
                for row in rows:
                    commands = ", ".join("Effect::" + e for e in row["effects"])
                    lines += ["    {%s, State::%s, {{%s}}, %d, %d}, // %s/%s %s" %
                              (mask(row["values"]), row["target"], commands, len(row["effects"]),
                               int(row["id"][1:]), state, event, row["id"])]
                offset += len(rows)
            else:
                code = matrix[state, event]
                cells[state, event] = "{CellKind::%s, 0, 0, Effect::%s}" % (kinds[code], policies[code])
    lines += ["};", "", "static constexpr std::uint64_t kValueDomains[] = {"]
    lines += ["    %s, // %s" % (mask(model["events"][event]), event) for event in events]
    lines += ["};", "", "static constexpr Event kEffectResults[] = {"]
    lines += ["    Event::%s, // %s" % (model["effect_results"].get(effect, "Count"), effect) for effect in effects]
    lines += ["};", "", "static constexpr Cell kCells[%d][%d] = {" % (len(states), len(events))]
    for state in states:
        lines += ["    { // " + state]
        lines += ["        %s, // %s" % (cells[state, event], event) for event in events]
        lines += ["    },"]
    lines += ["};", "", "} } // namespace Xm8Ra::MediaOperation", "", "#endif", ""]
    return "\n".join(lines)


def self_test(model):
    mutations = [
        lambda m: m["transitions"].pop(4),
        lambda m: m["transitions"][4]["values"].append("local"),
        lambda m: m["transitions"][0].update(target="Unknown"),
        lambda m: m["scenarios"][0]["effect_counts"].update(ApplyVm=2),
        lambda m: m["transitions"][0].update(id="T02"),
        lambda m: m["transitions"][3].update(effects=[]),
    ]
    for mutate in mutations:
        broken = deepcopy(model)
        mutate(broken)
        try:
            validate(broken)
        except ValueError:
            continue
        raise ValueError("validator accepted a deliberately broken model")
    print(f"PASS: {len(mutations)} invalid-model mutations rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check without writing")
    parser.add_argument("--self-test", action="store_true", help="exercise validation failures")
    args = parser.parse_args()
    model = json.loads(MODEL.read_text(encoding="utf-8"))
    matrix, by_id = validate(model)
    output = render(model, matrix, by_id)
    if args.self_test:
        self_test(model)
    for path, contents in [(OUTPUT, output), (CPP_OUTPUT, render_cpp(model, matrix))]:
        if args.check:
            require(path.exists() and path.read_text(encoding="utf-8") == contents,
                    f"generated output differs: {path}; run without --check")
        else:
            path.write_text(contents, encoding="utf-8")
    print(f"PASS: {len(matrix)} cells classified; {len(by_id)} transitions; "
          f"{len(model['scenarios'])} abstract traces; generated document and C++ table match")


if __name__ == "__main__":
    main()
