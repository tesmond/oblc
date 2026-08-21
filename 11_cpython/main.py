import brc

FILE_PATH = "../data/measurements.txt"


def c(path: str | None = None) -> str:
	"""Run the C implementation"""
	return brc.calculate(FILE_PATH)


if __name__ == "__main__":
	print(c())
