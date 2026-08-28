#!/usr/bin/env python3
"""Generate repository health metrics reports in text and HTML formats."""

from __future__ import annotations

import argparse
import html
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = REPO_ROOT / "docs" / "metrics"
TEXT_OUTPUT = OUTPUT_DIR / "repo-metrics.txt"
HTML_OUTPUT = OUTPUT_DIR / "repo-metrics.html"

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
    rel = path.relative_to(REPO_ROOT).as_posix()
    if any(rel.startswith(prefix) for prefix in EXCLUDED_PATH_PREFIXES):
        return False
    if path.name.startswith(".") and path.name not in {".gitignore", ".gitattributes"}:
        return False
    if any(part in EXCLUDED_DIRS for part in path.parts):
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
    if language in {"C++", "C", "C/C++ Header", "QML", "CMake", "XML", "CSS", "JavaScript", "TypeScript", "Qt Resource", "SVG"}:
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
            elif language == "CMake" and stripped.startswith("#"):
                comment_lines += 1
            elif language in {"CSS", "JavaScript", "TypeScript"} and stripped.startswith("*"):
                comment_lines += 1
        return comment_lines

    if language in {"Python", "Shell", "PowerShell", "Batch", "Docker", "Make", "Text", "Markdown", "YAML", "TOML", "INI", "JSON", "Properties", "SQL"}:
        comment_lines = 0
        for line in lines:
            stripped = line.lstrip()
            if language == "Batch" and stripped.upper().startswith("REM"):
                comment_lines += 1
            elif language in {"Python", "Shell", "PowerShell", "CMake", "Docker", "Make"} and stripped.startswith("#"):
                comment_lines += 1
            elif language in {"Text", "Markdown"}:
                if stripped.startswith("<!--") or stripped.startswith("#"):
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
    biggest_files: List[Tuple[int, str, int, int, int, int]] = []

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

        biggest_files.append((total_lines_for_file, rel_str, code_lines, blank_lines, comment_lines, len(lines)))

    biggest_files.sort(reverse=True)
    top_files = biggest_files[:15]

    return {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S %Z"),
        "repo_root": str(root),
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


def write_text_report(metrics: Dict[str, object], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "pimio repository health metrics",
        "===============================",
        f"generated: {metrics['generated_at']}",
        f"repository: {metrics['repo_root']}",
        f"commit: {metrics['commit']}",
        "",
        f"Included files: {metrics['files']}",
        f"Total lines: {metrics['lines']}",
        f"Code lines: {metrics['code_lines']}",
        f"Blank lines: {metrics['blank_lines']}",
        f"Comment lines: {metrics['comment_lines']}",
        "",
        "By language:",
    ]
    for language, count in sorted(metrics["language_counts"].items(), key=lambda item: (-item[1], item[0])):
        lines.append(f"- {language}: {count}")

    lines.extend(["", "By top-level directory:"])
    for directory, count in sorted(metrics["top_level_counts"].items(), key=lambda item: (-item[1], item[0])):
        lines.append(f"- {directory}: {count} files")

    lines.extend(["", "Largest files:"])
    for total_lines_for_file, rel_str, code_lines, blank_lines, comment_lines, _ in metrics["top_files"]:
        lines.append(
            f"- {rel_str}: {total_lines_for_file} lines ({code_lines} code, {blank_lines} blank, {comment_lines} comments)"
        )

    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_html_report(metrics: Dict[str, object], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for language, count in sorted(metrics["language_counts"].items(), key=lambda item: (-item[1], item[0])):
        rows.append(f"<tr><td>{html.escape(language)}</td><td>{count}</td></tr>")

    dir_rows = []
    for directory, count in sorted(metrics["top_level_counts"].items(), key=lambda item: (-item[1], item[0])):
        dir_rows.append(f"<tr><td>{html.escape(directory)}</td><td>{count}</td></tr>")

    file_rows = []
    for total_lines_for_file, rel_str, code_lines, blank_lines, comment_lines, _ in metrics["top_files"]:
        file_rows.append(
            f"<tr><td>{html.escape(rel_str)}</td><td>{total_lines_for_file}</td><td>{code_lines}</td><td>{blank_lines}</td><td>{comment_lines}</td></tr>"
        )

    html_content = f"""<!DOCTYPE html>
<html lang=\"en\">
  <head>
    <meta charset=\"utf-8\" />
    <title>pimio repository health metrics</title>
    <style>
      body {{ font-family: Arial, sans-serif; margin: 2rem; color: #1f2937; }}
      table {{ border-collapse: collapse; width: 100%; margin-bottom: 1.5rem; }}
      th, td {{ border: 1px solid #d1d5db; padding: 0.5rem; text-align: left; }}
      th {{ background: #f3f4f6; }}
      h1, h2 {{ margin-bottom: 0.5rem; }}
      .summary {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem; margin-bottom: 1.5rem; }}
      .summary div {{ border: 1px solid #e5e7eb; padding: 0.75rem; border-radius: 0.25rem; }}
    </style>
  </head>
  <body>
    <h1>pimio repository health metrics</h1>
    <p>Generated: {metrics['generated_at']}</p>
    <p>Commit: {metrics['commit']}</p>
    <div class=\"summary\">
      <div><strong>Included files</strong><br />{metrics['files']}</div>
      <div><strong>Total lines</strong><br />{metrics['lines']}</div>
      <div><strong>Code lines</strong><br />{metrics['code_lines']}</div>
      <div><strong>Blank lines</strong><br />{metrics['blank_lines']}</div>
      <div><strong>Comment lines</strong><br />{metrics['comment_lines']}</div>
    </div>

    <h2>By language</h2>
    <table>
      <thead><tr><th>Language</th><th>Files</th></tr></thead>
      <tbody>{''.join(rows)}</tbody>
    </table>

    <h2>By top-level directory</h2>
    <table>
      <thead><tr><th>Directory</th><th>Files</th></tr></thead>
      <tbody>{''.join(dir_rows)}</tbody>
    </table>

    <h2>Largest files</h2>
    <table>
      <thead><tr><th>Path</th><th>Total lines</th><th>Code lines</th><th>Blank lines</th><th>Comment lines</th></tr></thead>
      <tbody>{''.join(file_rows)}</tbody>
    </table>
  </body>
</html>
"""
    output_path.write_text(html_content, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="Repository root to scan")
    parser.add_argument("--text-output", type=Path, default=TEXT_OUTPUT, help="Path for the text report")
    parser.add_argument("--html-output", type=Path, default=HTML_OUTPUT, help="Path for the HTML report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    metrics = collect_metrics(root)
    write_text_report(metrics, args.text_output)
    write_html_report(metrics, args.html_output)
    print(f"Wrote {args.text_output}")
    print(f"Wrote {args.html_output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
