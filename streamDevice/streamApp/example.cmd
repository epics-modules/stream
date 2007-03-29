#!./streamApp

dbLoadDatabase "O.Common/streamApp.dbd"
streamApp_registerRecordDeviceDriver

#where can protocols be located?
epicsEnvSet "STREAM_PROTOCOL_PATH", ".:protocols:../protocols/"

#setup the busses
#drvAsynSerialPortConfigure "COM2", "/dev/ttyS1"
drvAsynIPPortConfigure "terminal", "localhost:40000"

#load the records
dbLoadRecords "example.db","PREFIX=DZ"
#dbLoadRecords "scalcout.db","PREFIX=DZ"

#lots of debug output
#var streamDebug 1

iocInit
