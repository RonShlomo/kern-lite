import serial
s = serial.Serial("COM4", 115200, timeout=3)
print(s.read(64))
s.close()