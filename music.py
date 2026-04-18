import pygame
import os

class MusicApp:
    def __init__(self, audio):
        self.audio = audio
        self.songs = os.listdir("/home/aeropod/music") if os.path.exists("/home/aeropod/music") else []
        self.index = 0
        pygame.mixer.init()

    def update(self, event):
        if event["up"]:
            self.index = (self.index - 1) % len(self.songs) if self.songs else 0
        if event["down"]:
            self.index = (self.index + 1) % len(self.songs) if self.songs else 0
        if event["select"]:
            self.play()
        if event["back"]:
            return "exit"

    def play(self):
        if not self.songs:
            return
        song = self.songs[self.index]
        pygame.mixer.music.load(f"/home/aeropod/music/{song}")
        pygame.mixer.music.play()
