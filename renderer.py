from PIL import Image, ImageDraw

class Renderer:
    def __init__(self, display):
        self.display = display

    def draw_menu(self, items, selected):
        img = Image.new("RGB", (240, 320), "black")
        draw = ImageDraw.Draw(img)

        draw.text((70, 10), "AEROPOD", fill="cyan")

        y = 50
        for i, item in enumerate(items):
            color = "cyan" if i == selected else "white"
            draw.text((30, y), item, fill=color)
            y += 40

        self.display.show(img)
