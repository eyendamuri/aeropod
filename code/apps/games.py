class GamesApp:
    def __init__(self):
        self.games = ["Snake", "Pong"]
        self.index = 0

    def update(self, event):
        if event["up"]:
            self.index = (self.index - 1) % len(self.games)
        if event["down"]:
            self.index = (self.index + 1) % len(self.games)
        if event["select"]:
            print("Launching", self.games[self.index])
        if event["back"]:
            return "exit"
