from PIL import Image, ImageDraw
import json
import math

IMAGE_WIDTH = 800
MAX_IMAGE_HEIGHT = 800
INPUT_FILENAME = "heapmap.out"

GRADIENT = ((0, 255, 0), (255, 0, 0))
def get_color(percentage):
    assert percentage >= 0 and percentage <= 1
    return (
        round(GRADIENT[0][0] + (GRADIENT[1][0] - GRADIENT[0][0]) * percentage),
        round(GRADIENT[0][1] + (GRADIENT[1][1] - GRADIENT[0][1]) * percentage),
        round(GRADIENT[0][2] + (GRADIENT[1][2] - GRADIENT[0][2]) * percentage)
    )

with open(INPUT_FILENAME, "r") as input:
    data = json.load(input)

n = math.ceil(math.sqrt(len(data["Bins"])))
m = n - math.floor((n ** 2 - len(data["Bins"])) / n)
total = 0
for sample in data["Bins"]:
    total += sample

square_size = IMAGE_WIDTH / n
image_height = math.ceil(m * square_size)
image = Image.new('RGB', (IMAGE_WIDTH, image_height), (255, 255, 255))
draw = ImageDraw.Draw(image)

for i in range(n):
    for j in range(n):
        color = (255, 255, 255)
        if j * n + i < len(data["Bins"]):
            percentage = data["Bins"][j * n + i] / total
            amplified_percentage = -(percentage - 1) ** 4 + 1
            color = get_color(amplified_percentage)
        draw.rectangle(
            (i * square_size, j * square_size, (i + 1) * square_size, (j + 1) * square_size), 
            fill=color,
            outline=(0, 0, 0)
        )

image.save("heapmap.jpg")