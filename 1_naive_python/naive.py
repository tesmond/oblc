import csv


path = "../data/measurements.txt"

with open(path) as csvfile:
    reader = csv.reader(csvfile, delimiter=';')
    result = {}
    for row in reader:
        city = row[0]
        temp_float = float(row[1])

        if city in result:
            item = result[city]
            item[0] += 1
            item[1] += temp_float
            item[2] = min(item[2], temp_float)
            item[3] = max(item[3], temp_float)
        else:
            result[city] = [1, temp_float, temp_float, temp_float]


print("{")
print(",".join(
    f"{city}={item[2]:.1f}/{item[1]/item[0]:.1f}/{item[3]:.1f}"
    for city, item in sorted(result.items())
), end="")
print("}")