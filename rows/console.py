"""Print messages to a relevant output stream."""

import sys
import os


class Console:
    """Print messages to a relevant output stream.

    Provides an abstraction over the standard output and the standard output for errors.

    The process could be started without a console window and with output streams redirected to a file.
    Therefore, every feature that requires printing to an output stream should use a relevant method
    of this class instead of directly interacting with the system IO."""

    def __init__(self, file=None):
        self.file = file
        if not self.file:
            self.file = sys.stdout

    def write(self, message):
        """Output message"""

        print(message, file=self.file)

    def write_line(self, message):
        """Output message and append a new line"""

        print(message, file=self.file, end=os.linesep)
