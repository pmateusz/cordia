import os


def real_path(file_path):
    return os.path.realpath(os.path.expandvars(os.path.expanduser(file_path)))
