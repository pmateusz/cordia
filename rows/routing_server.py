#!/usr/bin/env python3

import json
import logging
import os
import os.path
import subprocess


class RoutingServer:
    class Session:
        ENCODING = 'ascii'
        MESSAGE_TIMEOUT = 3
        EXIT_TIMEOUT = 5

        def __init__(self,
                     server_executable='./build/rows-routing-server',
                     maps_file='--maps=./data/scotland-latest.osrm'):
            self.__process = subprocess.Popen([server_executable, maps_file],
                                              stdin=subprocess.PIPE,
                                              stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE)

        def distance(self, source, destination):
            self.__process.stdin.write(
                json.dumps({'command': 'route',
                            'source': source.as_dict(),
                            'destination': destination.as_dict()}).encode(self.ENCODING))
            self.__process.stdin.write(os.linesep.encode(self.ENCODING))
            self.__process.stdin.flush()
            stdout_msg = self.__process.stdout.readline()
            if stdout_msg:
                message = json.loads(stdout_msg.decode(self.ENCODING))
                return message.get('distance', None)
            return None

        def close(self, exc, value, tb):
            stdout_msg, error_msg = self.__process.communicate('{"command":"shutdown"}'.encode(self.ENCODING),
                                                               timeout=self.MESSAGE_TIMEOUT)
            if error_msg:
                logging.error(error_msg)

            try:
                self.__process.wait(self.EXIT_TIMEOUT)
            except subprocess.TimeoutExpired:
                logging.exception('Failed to shutdown the routing server')
                self.__process.kill()
                self.__process.__exit__(exc, value, tb)

    def __init__(self, server_executable, maps_file):
        self.__server_executable = server_executable
        self.__maps_file = maps_file

    def __enter__(self):
        self.__session = RoutingServer.Session(self.__server_executable, self.__maps_file)
        return self.__session

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.__session.close(exc_type, exc_val, exc_tb)
