#!/usr/bin/env python3
"""공식 Laya 체크포인트를 로컬 C++ 런타임 패키지로 변환한다.

입력은 로컬 경로이다. 모델 다운로드, 양자화, 외부 코드 실행을 수행하지 않는다.
내보내기 입력과 동적 축은 Laya 0.3.24 scripts/export_onnx.py에서 가져왔다.
원본: Convai Innovations, Apache-2.0; third_party/laya/{NOTICE,LICENSE} 참조.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil

LAYA_VERSION = "0.3.24"


def export(checkpoint, output, revision=None):
    if importlib.metadata.version("laya") != LAYA_VERSION:
        raise RuntimeError(f"내보내기는 laya=={LAYA_VERSION}이 필요하다")
    import torch
    from laya.agent import Agent

    checkpoint = Path(checkpoint).resolve(strict=True)
    for name in ("model.safetensors", "rl_agent_config.json", "encoder/config.json",
                 "tokenizer/tokenizer.json", "tokenizer/tokenizer_config.json"):
        if not (checkpoint / name).is_file():
            raise FileNotFoundError(checkpoint / name)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    agent = Agent(str(checkpoint), compile=False, device="cpu")
    # 서로 다른 batch/sequence/marker 길이로 동적 축의 trace 고정을 방지한다.
    batch, length, markers = 2, 17, 3
    inputs = (torch.randint(0, 100, (batch, length), dtype=torch.long),
              torch.ones((batch, length), dtype=torch.long),
              torch.tensor([[1, 5, 9]] * batch, dtype=torch.long),
              torch.ones((batch, markers), dtype=torch.bool),
              torch.zeros(batch, dtype=torch.long))
    names = ["input_ids", "attention_mask", "marker_pos", "marker_mask", "qtype"]
    axes = {
        "input_ids": {0: "batch_size", 1: "seq_len"},
        "attention_mask": {0: "batch_size", 1: "seq_len"},
        "marker_pos": {0: "batch_size", 1: "num_markers"},
        "marker_mask": {0: "batch_size", 1: "num_markers"},
        "qtype": {0: "batch_size"},
        "logits": {0: "batch_size", 1: "num_markers"},
        "act_logits": {0: "batch_size"},
    }
    torch.onnx.export(agent.model, inputs, str(output / "laya.onnx"),
                      export_params=True, opset_version=18, do_constant_folding=True,
                      input_names=names, output_names=["logits", "act_logits"], dynamic_axes=axes)
    for name in ("rl_agent_config.json", "tokenizer/tokenizer.json", "tokenizer/tokenizer_config.json"):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        source = checkpoint / name
        if source != target:
            shutil.copy2(source, target)
    # ONNX 외부 가중치 파일도 함께 보존해야 한다.
    files = [p for p in output.rglob("*") if p.is_file() and
             (p.name in ("laya.onnx", "laya.onnx.data", "rl_agent_config.json",
                         "tokenizer.json", "tokenizer_config.json"))]
    def digest(path):
        result = hashlib.sha256()
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""):
                result.update(chunk)
        return result.hexdigest()
    manifest = {"schema": "iidecision-laya-v1", "laya_version": LAYA_VERSION,
                "source_revision": revision, "precision": "float32-unquantized",
                "sha256": {str(p.relative_to(output)): digest(p) for p in files}}
    (output / "iidecision_laya.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--revision", help="추적용 Hugging Face 체크포인트 커밋 SHA")
    args = parser.parse_args()
    print(export(args.checkpoint, args.output, args.revision))
