import json

import pytest

from debug_gym.gym.envs.local_dataset import LocalDataset, load_local_dataset

ROWS = [
    {"target_id": "proj::a.py:1", "level": "easy", "target_line": 1, "cve_pattern": ["x", "a.py:1"]},
    {"target_id": "proj::b.py:2", "level": "hard", "target_line": 2, "cve_pattern": ["y", "b.py:2"]},
]


def write_split(directory, split="test", rows=ROWS):
    directory.mkdir(parents=True, exist_ok=True)
    with open(directory / f"{split}.jsonl", "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row) + "\n")
        f.write("\n")  # blank lines are skipped


def test_load_local_dataset_rows_and_columns(tmp_path):
    write_split(tmp_path / "ds")
    ds = load_local_dataset(str(tmp_path / "ds"), "test", build_command="build it")
    assert isinstance(ds, LocalDataset)
    assert len(ds) == 2
    assert ds["target_id"] == ["proj::a.py:1", "proj::b.py:2"]
    assert ds[1] == ROWS[1]
    assert list(ds) == ROWS
    assert ds.column_names == ["target_id", "level", "target_line", "cve_pattern"]


def test_load_local_dataset_relative_to_cwd(tmp_path, monkeypatch):
    write_split(tmp_path / "ds")
    monkeypatch.chdir(tmp_path)
    assert len(load_local_dataset("ds", "test", build_command="build it")) == 2


def test_load_local_dataset_missing_dir(tmp_path):
    with pytest.raises(FileNotFoundError, match="python build.py --out-dir"):
        load_local_dataset(str(tmp_path / "missing"), "test", build_command="python build.py --out-dir x")


def test_load_local_dataset_missing_split(tmp_path):
    write_split(tmp_path / "ds", split="train")
    with pytest.raises(FileNotFoundError, match="test.jsonl"):
        load_local_dataset(str(tmp_path / "ds"), "test", build_command="build it")


def test_unknown_column_raises(tmp_path):
    write_split(tmp_path / "ds")
    ds = load_local_dataset(str(tmp_path / "ds"), "test", build_command="build it")
    with pytest.raises(KeyError):
        ds["no_such_column"]
