"""Compila ed esegue i tre test del protocollo senza dispositivi collegati."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    with tempfile.TemporaryDirectory(prefix="roger-edge1-tests-") as temporary:
        for name in ("protocol", "revision", "inputs"):
            output = Path(temporary) / name
            subprocess.run(
                compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                            "-I", str(ROOT / "components/roger_edge1"),
                            str(ROOT / f"tests/test_{name}.cpp"),
                            str(ROOT / "components/roger_edge1/protocol.cpp"),
                            "-o", str(output)], check=True,
            )
            subprocess.run([str(output)], check=True)
    print("Tre suite C++ completate.")


if __name__ == "__main__":
    main()
