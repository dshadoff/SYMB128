# -*- coding: cp1252 -*-

# This is an attempt at gettign data from the COM port into a file

import sys
import serial
import argparse

#
# this function saves a file from the MB128
#
def savefile(s, filename):
    with open(filename, 'wb') as f:
        s.write(b's')
        retry = 3
        expected = 131072
        while expected:
            data = s.read(expected)
            if data:
                f.write(data)
                expected -= len(data)
                retry = 3
            else:
                retry -= 1
                if not retry:
                    return False
    return True

#
# this function sends a file to the MB128)
#
def loadfile(s, filename):
    with open(filename, 'rb') as f:
        s.write(b'l')
        retry = 3
        sent = 0
        
        size = 131072
        data = f.read(size)
        
        while sent < size:
            written = s.write(data)
            if written:
                data = data[written:]
                sent += written
                retry = 3
            else:
                retry -= 1
                if not retry:
                    return False
    return True

#
# Mainline
#
# command-line format:
# python SYMB128.py <port> [S | L] <filename>
#
# where:
# port is in the form 'COM12'
# S = save to PC file / L = load from PC file
# filename is name of file on local PC
#

parser = argparse.ArgumentParser(add_help=True)
parser.add_argument('port', help='serial port')
parser.add_argument('filename', help='output filename')
group = parser.add_mutually_exclusive_group(required=True)
group.add_argument('-s', '--save', help='save to file', action="store_true")
group.add_argument('-l', '--load', help='load from file', action="store_true")
args = parser.parse_args()

filename = args.filename

com = serial.Serial()

com.baudrate = 115200
com.timeout = 4
com.write_timeout = 1
com.port = args.port

try:
    with com as s:
        ret = savefile(s, filename) if args.save else loadfile(s, filename)
        if not ret:
            print("[error] Operation failed")
except Exception as e:
    print(str(e))
