from pathlib import Path
from typing import Optional

import brc


DEFAULT_PATH = Path(__file__).resolve().parent.parent / "data" / "measurements.txt"


def c(path: str | None = None) -> str:
	"""Run the C implementation and return the formatted results set."""
	target = str(DEFAULT_PATH if path is None else Path(path))
	return brc.calculate(target)


if __name__ == "__main__":
	print(c())
