#!/usr/bin/env python3
"""Cargo metadata에 대응하는 원본 라이선스를 중복 없이 한 파일에 보존한다."""
import argparse
import json
from pathlib import Path


def collect(metadata, output):
    packages = json.loads(Path(metadata).read_text())["packages"]
    texts = {}
    entries = []
    for p in sorted(packages, key=lambda x: x["name"]):
        if p["source"] is None:
            continue
        folder = Path(p["manifest_path"]).parent
        files = sorted(f for f in folder.iterdir() if f.is_file() and
                       f.name.upper().startswith(("LICENSE", "COPYING", "COPYRIGHT", "NOTICE")))
        references = []
        for f in files:
            content = f.read_text(encoding="utf-8")
            if content not in texts:
                texts[content] = len(texts) + 1
            references.append(f"{f.name}: license text {texts[content]}")
        entries.append(f"Package: {p['name']} {p['version']}\n"
                       f"License expression: {p.get('license')}\n"
                       f"Source: {p.get('repository') or p['source']}\n" +
                       ("\n".join(references) if references else
                        "The published crate contains no separate license file; its declared license expression is recorded above."))
    document = "Hugging Face tokenizers and its locked Cargo dependencies.\nOriginal license text and notices follow.\n\n"
    document += "\n\n".join(entries)
    for content, index in texts.items():
        document += f"\n\n--- License text {index} ---\n{content}"
    Path(output).write_text(document, encoding="utf-8")
    return len(entries)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata")
    parser.add_argument("output")
    args = parser.parse_args()
    print(f"Recorded {collect(args.metadata, args.output)} packages")
