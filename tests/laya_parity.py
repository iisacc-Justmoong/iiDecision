#!/usr/bin/env python3
"""원본 Laya eager/ONNX와 C++ 백엔드를 실제 로컬 모델로 비교한다."""
import argparse
import json
from pathlib import Path
import subprocess

QUESTIONS = {
    "route": {"type": "choice", "instructions": "Choose the next action for this support ticket.",
              "criteria": {"refund": "Issue a refund", "troubleshoot": "Help fix the technical problem",
                           "escalate": "Send to a human specialist"}},
    "severity": {"type": "score", "instructions": "Rate the severity of the issue.", "criteria": ["Low", "Medium", "High"]},
    "urgent": {"type": "noul", "instructions": "Does the customer require immediate help?",
               "criteria": {"false": "The request can wait", "true": "Immediate help is needed"}},
}
STATES = [
    "The application crashes on startup and the customer cannot access a paid project.",
    "The customer was charged twice and requests their money back.",
    "The user is asking how to change the color of a button. There is no urgency.",
    "[MASK] The customer says: café, naïve, 한국어, 日本語, emoji 😀.",
    "",
    "An unrelated status entry. " * 300 + "The project is inaccessible.",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--driver", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    from laya.agent import Agent
    from laya.onnx_agent import ONNXAgent
    from laya.common import build_sequence, collate_items, temp_bucket
    import numpy as np

    package = Path(args.package).resolve()
    print("Loading official eager and ONNX agents", flush=True)
    eager = Agent(args.checkpoint, compile=False, device="cpu")
    onnx = ONNXAgent(args.checkpoint, onnx_path=str(package / "laya.onnx"))
    print("Running C++ decision backend", flush=True)
    native = subprocess.run([args.driver, str(package)], input="\n".join(STATES) + "\n",
                            text=True, capture_output=True, check=True, timeout=300)
    lines = iter(native.stdout.splitlines())
    records = []
    max_native_onnx = max_eager_onnx = max_native_raw = 0.0
    for state_index, state in enumerate(STATES):
        reference = onnx.predict(state, QUESTIONS)
        torch_reference = eager.predict(state, QUESTIONS)
        internal_questions = {qid: onnx._to_internal(q) for qid, q in QUESTIONS.items()}
        items = onnx._encode_state(state, list(QUESTIONS), internal_questions,
                                   onnx.cfg.get("max_len", 512), onnx.cfg.get("head_max_len", 192))
        tensors = collate_items([items], onnx.tok.pad_token_id)
        inputs = {name: tensors[name].numpy() for name in ("input_ids", "attention_mask", "marker_pos", "marker_mask", "qtype")}
        raw_logits = onnx.session.run(["logits", "act_logits"], inputs)[0]
        for row, (qid, definition) in enumerate(QUESTIONS.items()):
            cols = next(lines).split("\t")
            assert cols[0] == qid
            floats = list(map(float, cols[2:7]))
            counts = list(map(int, cols[7:11]))
            probs = np.array(list(map(float, cols[11:])))
            actual = reference["answers"][qid]
            pytorch = torch_reference["answers"][qid]
            internal = onnx._to_internal(definition)
            ids, markers, stats, trunc = build_sequence(onnx.tok, state, internal,
                max_len=onnx.cfg.get("max_len", 512), head_max_len=onnx.cfg.get("head_max_len", 192),
                return_stats=True, return_truncation_stats=True)
            expected_probs = np.array(list(actual["probabilities"].values())) if "probabilities" in actual else np.array([1-actual["noul"], actual["noul"]])
            expected_eager = np.array(list(pytorch["probabilities"].values())) if "probabilities" in pytorch else np.array([1-pytorch["noul"], pytorch["noul"]])
            delta = float(np.max(np.abs(probs-expected_probs)))
            qt = items[row]["qtype"]
            temperature = onnx.temperature_by_options.get(temp_bucket(qt,len(markers)),onnx.temperature[qt])
            z = raw_logits[row,:len(markers)].astype(np.float64)/temperature
            raw_probs = np.exp(z-z.max()); raw_probs /= raw_probs.sum()
            raw_delta = float(np.max(np.abs(probs-raw_probs)))
            eager_delta = float(np.max(np.abs(expected_eager-expected_probs)))
            max_native_onnx = max(max_native_onnx, delta)
            max_eager_onnx = max(max_eager_onnx, eager_delta)
            max_native_raw = max(max_native_raw,raw_delta)
            # 원본 공개 JSON은 4자리로 반올림한다. 비교 허용치는 반올림 오차 + 작은 FP32 차이이다.
            assert delta < 1e-4, (state_index, qid, delta)
            assert eager_delta < 2e-4, (state_index, qid, eager_delta)
            assert raw_delta < 1e-5, (state_index, qid, raw_delta)
            for got, expected in zip(floats[:2], [actual["confidence"],actual["answer_confidence"]]):
                assert abs(got-expected) < 1e-4
            assert abs(floats[4]-actual["action"]["act_probability"]) < 1e-4
            if definition["type"] == "choice":
                assert cols[1] == actual["choice"] == pytorch["choice"]
            elif definition["type"] == "score":
                assert abs(floats[2]-actual["score"]) < 2e-4
            else:
                assert abs(floats[3]-actual["noul"]) < 1e-4
            assert counts == [len(ids),trunc["state_tokens"],trunc["state_tokens_dropped"],stats["options_distinct"]]
            records.append({"state":state_index,"question":qid,"selected":cols[1],
                            "native_onnx_max_probability_delta":delta,"eager_onnx_max_probability_delta":eager_delta,
                            "native_raw_onnx_max_probability_delta":raw_delta,
                            "input_tokens":counts[0],"state_tokens":counts[1],"dropped":counts[2]})
        print(f"Verified state {state_index + 1}/{len(STATES)}", flush=True)
    assert next(lines, None) is None
    result = {"states":len(STATES),"questions":len(records),"native_onnx_max_delta":max_native_onnx,
              "eager_onnx_max_delta":max_eager_onnx,"native_raw_onnx_max_delta":max_native_raw,"records":records}
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in result.items() if k!="records"}))


if __name__ == "__main__":
    main()
