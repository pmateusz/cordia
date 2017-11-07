import sys
import os


class Console:
    """Print output messages"""

    def __init__(self, file=None):
        self.file = file
        if not self.file:
            self.file = sys.stdout

    def write(self, message):
        print(message, file=self.file)

    def write_line(self, message):
        print(message, file=self.file, end=os.linesep)
