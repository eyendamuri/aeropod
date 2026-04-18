class Menu:
    def __init__(self, items):
        self.items = items
        self.index = 0

    def update(self, event):
        if event["up"]:
            self.index = (self.index - 1) % len(self.items)
        if event["down"]:
            self.index = (self.index + 1) % len(self.items)

    def get_selected(self):
        return self.items[self.index]

    def render(self):
        return self.items, self.index
