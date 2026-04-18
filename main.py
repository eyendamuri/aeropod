from input.clickwheel import ClickWheel
from ui.menu import Menu
from ui.renderer import Renderer

from apps.music import MusicApp
from apps.camera import CameraApp
from apps.games import GamesApp

from hardware.display import Display
from hardware.audio import AudioSystem

class AeropodOS:
    def __init__(self):

        self.display = Display()
        self.audio = AudioSystem()

        self.input = ClickWheel(num_segments=12)

        self.apps = {
            "Music": MusicApp(self.audio),
            "Camera": CameraApp(),
            "Games": GamesApp()
        }

        self.menu = Menu(list(self.apps.keys()))
        self.renderer = Renderer(self.display)

        self.state = "menu"
        self.active_app = None

    def run(self):
        while True:
            event = self.input.read()

            if self.state == "menu":
                self.menu.update(event)

                items, selected = self.menu.render()
                self.renderer.draw_menu(items, selected)

                if event["select"]:
                    app_name = self.menu.get_selected()
                    self.active_app = self.apps[app_name]
                    self.state = "app"

            elif self.state == "app":
                result = self.active_app.update(event)

                if result == "exit":
                    self.state = "menu"
                    self.active_app = None

if __name__ == "__main__":
    AeropodOS().run()
