"""Local datasets of the benchmark environments.

A dataset is a directory with one JSON Lines file per split (`<dir>/<split>.jsonl`, one row per
line), written by a build script under data/: data/dfuzzbench/build_dataset.py --out-dir (T1-T5)
or data/arvo/construct.py --out-dir (T6, added by arvo.patch). Everything is read from local
files; nothing is downloaded.
"""

import json
from pathlib import Path

AGENTIC_DIR = Path(__file__).resolve().parents[3]


class LocalDataset:
    """The rows of one split, in file order.

    `ds[i]` is row i (a dict), `ds["<column>"]` the list of that column's values, `len(ds)` the
    number of rows.
    """

    def __init__(self, rows: list[dict]):
        self.rows = rows

    def __len__(self) -> int:
        return len(self.rows)

    def __iter__(self):
        return iter(self.rows)

    def __getitem__(self, key: int | str):
        if isinstance(key, str):
            return [row[key] for row in self.rows]
        return self.rows[key]

    @property
    def column_names(self) -> list[str]:
        return list(self.rows[0]) if self.rows else []


def resolve_dataset_dir(dataset_dir: str, build_command: str) -> Path:
    """The dataset directory, relative to the working directory or to agentic/."""
    for candidate in (Path(dataset_dir), AGENTIC_DIR / dataset_dir):
        if candidate.is_dir():
            return candidate
    raise FileNotFoundError(
        f"Dataset directory `{dataset_dir}` not found. Build it from agentic/ with `{build_command}`."
    )


def load_local_dataset(dataset_dir: str, split: str, build_command: str) -> LocalDataset:
    """Reads `<dataset_dir>/<split>.jsonl`. build_command is the command that writes the dataset,
    shown when it is missing."""
    path = resolve_dataset_dir(dataset_dir, build_command) / f"{split}.jsonl"
    if not path.is_file():
        raise FileNotFoundError(
            f"Dataset file `{path}` not found. Build it from agentic/ with `{build_command}`."
        )
    with open(path, encoding="utf-8") as f:
        rows = [json.loads(line) for line in f if line.strip()]
    return LocalDataset(rows)
