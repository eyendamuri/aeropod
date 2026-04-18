import math
import time

class ClickWheel:
    def __init__(self, num_segments=12):
        self.n = num_segments
        self.angle_per_segment = 360 / self.n
        self.last_angle = None

    def read_sensors(self):
        return None

    def index_to_angle(self, index):
        return index * self.angle_per_segment

    def read(self):
        index = self.read_sensors()

        event = {
            "up": False,
            "down": False,
            "left": False,
            "right": False,
            "select": False,
            "back": False
        }

        if index is None:
            return event

        angle = self.index_to_angle(index)

        if self.last_angle is None:
            self.last_angle = angle
            return event

        delta = angle - self.last_angle

        if delta > 180:
            delta -= 360
        if delta < -180:
            delta += 360

        if abs(delta) > 5:
            if delta > 0:
                event["down"] = True
            else:
                event["up"] = True

        self.last_angle = angle
        return event
