# -*- coding: cp1252 -*-

# This is an attempt at gettign data from the COM port into a file

import sys
import serial

#
# this function saves a file from the MB128
#
def savefile(fname):
    f = open(fname, 'wb')
    ser.write(b's')
    countdown = 10000000
    count = 0

    while ((countdown > 0) and (count < 131072)):
        b = ser.read(ser.in_waiting or 1)
        if b:
            f.write(b)
            count = count + len(b)
        countdown = countdown - 1

    if (countdown != 0):
        return(0)
    else:
        return(1)

#
# this function sends a file to the MB128)
#
def loadfile(fname):
    f = open(fname, 'rb')
    ser.write(b'l')
    countdown = 10000000
    count = 0

    while(count < 131072):
        b = f.read(1)
        ser.write(b)
        count = count + 1

    return(0)

def usage():
    print('command-line format:')
    print('python SYMB128.py <port> [S | L] <filename>')
    print('')
    print('where:')
    print('port is in the form "COM12"')
    print('S = save to PC file / L = load from PC file')
    print('filename is name of file on local PC')
    return(0)

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

if (len(sys.argv) != 4):
    print(len(sys.argv), ' arguments; should be 4')
    usage()
    exit(1)

if (sys.argv[1][0:3] != 'COM'):
    print('Please use a COM port starting with "COM".')
    print(' ')
    usage()
    exit(1)

if (sys.argv[2] == 'S' or sys.argv[2] == 's'):
    operation = 'S'
elif (sys.argv[2] == 'L' or sys.argv[2] == 'l'):
    operation = 'L'
else:
    print('operation must be either "S" (save) or "L" (load)')
    print(' ')
    usage()
    exit(1)

filename = sys.argv[3]

ser = serial.Serial()
ser.baudrate = 115200
ser.timeout = 0
ser.port = sys.argv[1]

try:
    ser.open()
except:
    print("could not open port", ser.port)
    exit(1)

if (operation == 'S'):
    f = savefile(filename)
elif (operation == 'L'):
    f = loadfile(filename)

if (f == 1):
    print("Operation not successful")

ser.close()

