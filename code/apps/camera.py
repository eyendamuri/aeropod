import subprocess
import time

class CameraApp:
    def update(self, event):
        if event["select"]:
            filename = f"/home/aeropod/photos/{int(time.time())}.jpg"
            subprocess.run(["libcamera-jpeg","-o",filename,"--width","640","--height","480"])
        if event["back"]:
            return "exit"
