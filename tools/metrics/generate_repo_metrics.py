#!/usr/bin/env python3
"""Generate repository health metrics reports in Markdown format."""

from __future__ import annotations

import argparse
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = REPO_ROOT / "docs" / "metrics"
MARKDOWN_OUTPUT = OUTPUT_DIR / "repo-metrics.md"

EXCLUDED_DIRS = {".git", ".cache", "build", "install"}
EXCLUDED_PATH_PREFIXES = ("docs/metrics/", "tools/metrics/")

LANGUAGE_BY_EXTENSION = {
    ".c": "C",
    ".cc": "C++",
    ".cpp": "C++",
    ".cxx": "C++",
    ".c++": "C++",
    ".h": "C/C++ Header",
    ".hh": "C/C++ Header",
    ".hpp": "C/C++ Header",
    ".hxx": "C/C++ Header",
    ".ipp": "C/C++ Header",
    ".qml": "QML",
    ".py": "Python",
    ".sh": "Shell",
    ".ps1": "PowerShell",
    ".bat": "Batch",
    ".cmake": "CMake",
    ".yml": "YAML",
    ".yaml": "YAML",
    ".json": "JSON",
    ".toml": "TOML",
    ".ini": "INI",
    ".cfg": "INI",
    ".conf": "INI",
    ".txt": "Text",
    ".md": "Markdown",
    ".rst": "Text",
    ".xml": "XML",
    ".css": "CSS",
    ".js": "JavaScript",
    ".ts": "TypeScript",
    ".jsx": "JavaScript",
    ".tsx": "TypeScript",
    ".sql": "SQL",
    ".pro": "Qt Project",
    ".pri": "Qt Project",
    ".qmlproject": "Qt Project",
    ".qrc": "Qt Resource",
    ".svg": "SVG",
    ".properties": "Properties",
    ".gitignore": "Text",
    ".gitattributes": "Text",
}

FILENAME_LANGUAGE = {
    "CMakeLists.txt": "CMake",
    "Dockerfile": "Docker",
    "Makefile": "Make",
    "README": "Text",
    "LICENSE": "Text",
    "NOTICE": "Text",
}


def is_supported_path(path: Path) -> bool:
    rel = path.relative_to(REPO_ROOT)
    rel_posix = rel.as_posix()
    if any(rel_posix.startswith(prefix) for prefix in EXCLUDED_PATH_PREFIXES):
        return False
    if path.name.startswith(".") and path.name not in {".gitignore", ".gitattributes"}:
        return False
    if any(part in EXCLUDED_DIRS for part in rel.parts):
        return False
    return True


def detect_language(path: Path) -> str:
    if path.name in FILENAME_LANGUAGE:
        return FILENAME_LANGUAGE[path.name]
    suffix = path.suffix.lower()
    return LANGUAGE_BY_EXTENSION.get(suffix, "Other")


def is_text_file(path: Path) -> bool:
    if not path.is_file():
        return False
    if not is_supported_path(path):
        return False
    if detect_language(path) == "Other":
        return False
    return True


def count_comment_lines(lines: List[str], language: str) -> int:
    if language in {"C++", "C", "C/C++ Header", "QML", "XML", "CSS", "JavaScript", "TypeScript", "Qt Resource", "SVG"}:
        comment_lines = 0
        in_block_comment = False
        for line in lines:
            stripped = line.lstrip()
            if in_block_comment:
                comment_lines += 1
                if "*/" in line:
                    in_block_comment = False
                continue
            if stripped.startswith("/*"):
                comment_lines += 1
                if "*/" not in stripped:
                    in_block_comment = True
            elif stripped.startswith("//"):
                comment_lines += 1
            elif language in {"CSS", "JavaScript", "TypeScript"} and stripped.startswith("*"):
                comment_lines += 1
        return comment_lines

    if language in {"CMake", "Python", "Shell", "PowerShell", "Batch", "Docker", "Make", "Text", "Markdown", "YAML", "TOML", "INI", "Properties", "SQL"}:
        comment_lines = 0
        for line in lines:
            stripped = line.lstrip()
            if language == "Batch" and stripped.upper().startswith("REM"):
                comment_lines += 1
            elif language in {"CMake", "Python", "Shell", "PowerShell", "Docker", "Make", "YAML", "TOML", "INI", "Properties"} and stripped.startswith("#"):
                comment_lines += 1
            elif language in {"Text", "Markdown"} and (stripped.startswith("<!--") or stripped.startswith("#")):
                comment_lines += 1
            elif language == "SQL" and (stripped.upper().startswith("--") or stripped.upper().startswith("/*")):
                comment_lines += 1
        return comment_lines

    return 0


def collect_metrics(root: Path) -> Dict[str, object]:
    files = [path for path in root.rglob("*") if path.is_file() and is_text_file(path)]
    files.sort(key=lambda path: path.relative_to(root).as_posix())

    total_files = 0
    total_lines = 0
    total_code_lines = 0
    total_blank_lines = 0
    total_comment_lines = 0
    language_counts: Counter[str] = Counter()
    top_level_counts: Counter[str] = Counter()
    biggest_files: List[Tuple[int, str, int, int, int]] = []

    for path in files:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue

        rel = path.relative_to(root)
        rel_str = rel.as_posix()
        language = detect_language(path)
        language_counts[language] += 1

        parts = rel.parts
        top_level = parts[0] if parts else ""
        top_level_counts[top_level] += 1

        lines = text.splitlines()
        total_lines_for_file = len(lines)
        blank_lines = sum(1 for line in lines if not line.strip())
        comment_lines = count_comment_lines(lines, language)
        code_lines = max(total_lines_for_file - blank_lines - comment_lines, 0)

        total_files += 1
        total_lines += total_lines_for_file
        total_blank_lines += blank_lines
        total_comment_lines += comment_lines
        total_code_lines += code_lines

        biggest_files.append((total_lines_for_file, rel_str, code_lines, blank_lines, comment_lines))

    biggest_files.sort(reverse=True)
    top_files = biggest_files[:15]

    return {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S %Z"),
        "repo_name": root.name,
        "commit": get_git_commit(root),
        "files": total_files,
        "lines": total_lines,
        "code_lines": total_code_lines,
        "blank_lines": total_blank_lines,
        "comment_lines": total_comment_lines,
        "language_counts": language_counts,
        "top_level_counts": top_level_counts,
        "top_files": top_files,
    }


def get_git_commit(root: Path) -> str:
    try:
        completed = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        return "unknown"
    if completed.returncode != 0:
        return "unknown"
    return completed.stdout.strip()


def write_markdown_report(metrics: Dict[str, object], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# pimio repository health metrics",
        "",
        f"- Generated: {metrics['generated_at']}",
        f"- Repository: {metrics['repo_name']}",
        f"- Commit: {metrics['commit']}",
        "",
        "## Summary",
        f"- Included files: {metrics['files']}",
        f"- Total lines: {metrics['lines']}",
        f"- Code lines: {metrics['code_lines']}",
        f"- Blank lines: {metrics['blank_lines']}",
        f"- Comment lines: {metrics['comment_lines']}",
        "",
        "## By language",
        "",
        "| Language | Files |",
        "| --- | ---: |",
    ]
    for language, count in sorted(metrics["language_counts"].items(), key=lambda item: (-item[1], item[0])):
        lines.append(f"| {language} | {count} |")

    lines.extend(["", "## By top-level directory", "", "| Directory | Files |", "| --- | ---: |"])
    for directory, count in sorted(metrics["top_level_counts"].items(), key=lambda item: (-item[1], item[0])):
        lines.append(f"| {directory} | {count} |")

    lines.extend(["", "## Largest files", "", "| Path | Total lines | Code lines | Blank lines | Comment lines |", "| --- | ---: | ---: | ---: | ---: |"])
    for total_lines_for_file, rel_str, code_lines, blank_lines, comment_lines in metrics["top_files"]:
        lines.append(
            f"| {rel_str} | {total_lines_for_file} | {code_lines} | {blank_lines} | {comment_lines} |"
        )

    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="Repository root to scan")
    parser.add_argument("--output", type=Path, default=MARKDOWN_OUTPUT, help="Path for the Markdown report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    metrics = collect_metrics(root)
    write_markdown_report(metrics, args.output)
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
