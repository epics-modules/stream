/***************************************************************
* StreamDevice Support                                         *
*                                                              *
* (C) 2005 Dirk Zimoch (dirk.zimoch@psi.ch)                    *
*                                                              *
* This is the interface to asyn driver for StreamDevice.       *
* Please refer to the HTML files in ../doc/ for a detailed     *
* documentation.                                               *
*                                                              *
* If you do any changes in this file, you are not allowed to   *
* redistribute it any more. If there is a bug or a missing     *
* feature, send me an email and/or your patch. If I accept     *
* your changes, they will go to the next release.              *
*                                                              *
* DISCLAIMER: If this software breaks something or harms       *
* someone, it's your problem.                                  *
*                                                              *
***************************************************************/

#include "devStream.h"
#include "StreamBusInterface.h"
#include "StreamError.h"
#include "StreamBuffer.h"

#ifdef EPICS_3_14
#include <epicsAssert.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#else
#include <assert.h>
#include <wdLib.h>
#include <sysLib.h>
extern "C" {
#include <callback.h>
}
#endif

#include <asynDriver.h>
#include <asynOctet.h>
#include <asynInt32.h>
#include <asynUInt32Digital.h>
#include <asynGpibDriver.h>

/* How things are implemented:

synchonous io:

lockRequest()
    call pasynManager->blockProcessCallback(),
    call pasynCommon->connect() only if
        lockTimeout is unlimited (0) and port is not yet connected
    call pasynManager->queueRequest()
    when request is handled
        call lockCallback(ioSuccess)
    if request fails
        call lockCallback(ioTimeout)

writeRequest()
    call pasynManager->queueRequest()
    when request is handled
        call pasynOctet->flush()
        call pasynOctet->writeRaw()
        if writeRaw() times out
            call writeCallback(ioTimeout)
        if writeRaw fails otherwise
            call writeCallback(ioFault)
        if writeRaw succeeds and all bytes have been written
            call writeCallback(ioSuccess)
        if not all bytes can be written
            call pasynManager->queueRequest() again
    if request fails
        call writeCallback(ioTimeout)

readRequest()
    call pasynManager->queueRequest()
    when request is handled
        call pasynOctet->setInputEos()
        call pasynOctet->read()
        if read() times out at the first byte
            call readCallback(ioNoReply)
        if read() times out at next bytes
            call readCallback(ioTimeout,buffer,received)
        if read() fails otherwise
            call readCallback(ioFault,buffer,received)
        if read() is successfull and reason is ASYN_EOM_END
            or ASYN_EOM_EOS
            call readCallback(ioEnd,buffer,received)
        if read() is successfull and no end is detected
            call readCallback(ioSuccess,buffer,received)
        if readCallback() has returned true (wants more input)
            loop back to the pasynOctet->read() call
    if request fails
        call readCallback(ioFault)

unlock()
    call pasynManager->unblockProcessCallback()

asynchonous input support ("I/O Intr"):

pasynOctet->registerInterruptUser(...,intrCallbackOctet,...) is called
initially. This calls intrCallbackOctet() every time input is received,
but only if someone else is doing a read. Thus, if nobody reads
something, arrange for periodical read polls.

*/

extern "C" {
static void handleRequest(asynUser*);
static void handleTimeout(asynUser*);
static void intrCallbackOctet(void* pvt, asynUser *pasynUser,
    char *data, size_t numchars, int eomReason);
static void intrCallbackInt32(void* pvt, asynUser *pasynUser,
    epicsInt32 data);
static void intrCallbackUInt32(void* pvt, asynUser *pasynUser,
    epicsUInt32 data);
}

enum IoAction {
    None, Lock, Write, Read, AsyncRead, AsyncReadMore, AsyncReadCancelled,
    ReceiveEvent, Connect, Disconnect
};

static const char* ioActionStr[] = {
    "None", "Lock", "Write", "Read",
    "AsyncRead", "AsyncReadMore", "AsyncReadCancelled",
    "ReceiveEvent", "Connect", "Disconnect"
};

class AsynDriverInterface : StreamBusInterface
#ifdef EPICS_3_14
 , epicsTimerNotify
#endif
{
    asynUser* pasynUser;
    asynCommon* pasynCommon;
    void* pvtCommon;
    asynOctet* pasynOctet;
    void* pvtOctet;
    void* intrPvtOctet;
    asynInt32* pasynInt32;
    void* pvtInt32;
    void* intrPvtInt32;
    asynUInt32Digital* pasynUInt32;
    void* pvtUInt32;
    void* intrPvtUInt32;
    asynGpib* pasynGpib;
    void* pvtGpib;
    IoAction ioAction;
    double lockTimeout;
    double writeTimeout;
    double readTimeout;
    double replyTimeout;
    long expectedLength;
    unsigned long eventMask;
    unsigned long receivedEvent;
    StreamBuffer inputBuffer;
    const char* outputBuffer;
    size_t outputSize;
    int peeksize;
#ifdef EPICS_3_14
    epicsTimerQueueActive* timerQueue;
    epicsTimer* timer;
#else
    WDOG_ID timer;
    CALLBACK timeoutCallback;
#endif

    AsynDriverInterface(Client* client);
    ~AsynDriverInterface();

    // StreamBusInterface methods
    bool lockRequest(unsigned long lockTimeout_ms);
    bool unlock();
    bool writeRequest(const void* output, size_t size,
        unsigned long writeTimeout_ms);
    bool readRequest(unsigned long replyTimeout_ms,
        unsigned long readTimeout_ms, long expectedLength, bool async);
    bool acceptEvent(unsigned long mask, unsigned long replytimeout_ms);
    bool supportsEvent();
    bool supportsAsyncRead();
    bool connectRequest(unsigned long connecttimeout_ms);
    bool disconnect();
    void cancelAll();

#ifdef EPICS_3_14
    // epicsTimerNotify methods
    epicsTimerNotify::expireStatus expire(const epicsTime &);
#else
    static void expire(CALLBACK *pcallback);
#endif

    // local methods
    void timerExpired();
    bool connectToBus(const char* busname, int addr);
    void lockHandler();
    void writeHandler();
    void readHandler();
    void connectHandler();
    void disconnectHandler();
    bool connectToAsynPort();
    void asynReadHandler(char *data, size_t numchars);
    asynQueuePriority priority() {
        return static_cast<asynQueuePriority>
            (StreamBusInterface::priority());
    }
    void startTimer(double timeout) {
#ifdef EPICS_3_14
        timer->start(*this, timeout);
#else
        callbackSetPriority(priority(), &timeoutCallback);
        wdStart(timer, (int)((timeout+1)*sysClkRateGet())-1,
            reinterpret_cast<FUNCPTR>(callbackRequest),
            reinterpret_cast<int>(&timeoutCallback));
#endif
    }
    void cancelTimer() {
#ifdef EPICS_3_14
        timer->cancel();
#else
        wdCancel(timer);
#endif
    }

    // asynUser callback functions
    friend void handleRequest(asynUser*);
    friend void handleTimeout(asynUser*);
    friend void intrCallbackOctet(void* pvt, asynUser *pasynUser,
        char *data, size_t numchars, int eomReason);
    friend void intrCallbackInt32(void* pvt, asynUser *pasynUser,
        epicsInt32 data);
    friend void intrCallbackUInt32(void* pvt, asynUser *pasynUser,
        epicsUInt32 data);
public:
    // static creator method
    static StreamBusInterface* getBusInterface(Client* client,
        const char* busname, int addr, const char* param);
};

RegisterStreamBusInterface(AsynDriverInterface);

AsynDriverInterface::
AsynDriverInterface(Client* client) : StreamBusInterface(client)
{
    pasynCommon = NULL;
    pasynOctet = NULL;
    intrPvtOctet = NULL;
    pasynInt32 = NULL;
    intrPvtInt32 = NULL;
    pasynUInt32 = NULL;
    intrPvtUInt32 = NULL;
    pasynGpib = NULL;
    eventMask = 0;
    receivedEvent = 0;
    peeksize = 1;
    pasynUser = pasynManager->createAsynUser(handleRequest,
        handleTimeout);
    assert(pasynUser);
    pasynUser->userPvt = this;
#ifdef EPICS_3_14
    timerQueue = &epicsTimerQueueActive::allocate(true);
    assert(timerQueue);
    timer = &timerQueue->createTimer();
    assert(timer);
#else
    timer = wdCreate();
    callbackSetCallback(expire, &timeoutCallback);
    callbackSetUser(this, &timeoutCallback);
#endif
}

AsynDriverInterface::
~AsynDriverInterface()
{
    cancelTimer();

    if (intrPvtInt32)
    {
        // Int32 event interface is connected
        pasynInt32->cancelInterruptUser(pvtInt32,
            pasynUser, intrPvtInt32);
    }
    if (intrPvtUInt32)
    {
        // UInt32 event interface is connected
        pasynUInt32->cancelInterruptUser(pvtUInt32,
            pasynUser, intrPvtUInt32);
    }
    if (pasynOctet)
    {
        // octet stream interface is connected
        int wasQueued;
        if (intrPvtOctet)
        {
            pasynOctet->cancelInterruptUser(pvtOctet,
                pasynUser, intrPvtOctet);
        }
        pasynManager->cancelRequest(pasynUser, &wasQueued);
        // does not return until running handler has finished
    }
    // Now, no handler is running any more and none will start.

#ifdef EPICS_3_14
    timer->destroy();
    timerQueue->release();
#else
    wdDelete(timer);
#endif
    pasynManager->disconnect(pasynUser);
    pasynManager->freeAsynUser(pasynUser);
    pasynUser = NULL;
}

// interface function getBusInterface():
// do we have this bus/addr ?
StreamBusInterface* AsynDriverInterface::
getBusInterface(Client* client,
    const char* busname, int addr, const char*)
{
    AsynDriverInterface* interface = new AsynDriverInterface(client);
    if (interface->connectToBus(busname, addr))
    {
        debug ("AsynDriverInterface::getBusInterface(%s, %d): "
            "new Interface allocated\n",
            busname, addr);
        return interface;
    }
    delete interface;
    return NULL;
}

// interface function supportsEvent():
// can we handle the StreamDevice command 'event'?
bool AsynDriverInterface::
supportsEvent()
{
    return (pasynInt32 != NULL) || (pasynUInt32 != NULL);
}

bool AsynDriverInterface::
supportsAsyncRead()
{
    if (intrPvtOctet) return true;

    // hook "I/O Intr" support
    if (pasynOctet->registerInterruptUser(pvtOctet, pasynUser,
        intrCallbackOctet, this, &intrPvtOctet) != asynSuccess)
    {
        error("%s: bus does not support asynchronous input: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    return true;
}

bool AsynDriverInterface::
connectToBus(const char* busname, int addr)
{
    if (pasynManager->connectDevice(pasynUser, busname, addr) !=
        asynSuccess)
    {
        // asynDriver does not know this busname/address
        return false;
    }

    asynInterface* pasynInterface;

    // find the asynCommon interface
    pasynInterface = pasynManager->findInterface(pasynUser,
        asynCommonType, true);
    if(!pasynInterface)
    {
        error("%s: bus %s does not support asynCommon interface\n",
            clientName(), busname);
        return false;
    }
    pasynCommon = static_cast<asynCommon*>(pasynInterface->pinterface);
    pvtCommon = pasynInterface->drvPvt;

    // find the asynOctet interface
    pasynInterface = pasynManager->findInterface(pasynUser,
        asynOctetType, true);
    if(!pasynInterface)
    {
        error("%s: bus %s does not support asynOctet interface\n",
            clientName(), busname);
        return false;
    }
    pasynOctet = static_cast<asynOctet*>(pasynInterface->pinterface);
    pvtOctet = pasynInterface->drvPvt;

    // is it a GPIB interface ?
    pasynInterface = pasynManager->findInterface(pasynUser,
        asynGpibType, true);
    if(pasynInterface)
    {
        pasynGpib = static_cast<asynGpib*>(pasynInterface->pinterface);
        pvtGpib = pasynInterface->drvPvt;
        // asynGpib returns overflow error if we try to peek
        // (read only one byte first).
        peeksize = 100;
    }

    // look for interfaces for events
    pasynInterface = pasynManager->findInterface(pasynUser,
        asynInt32Type, true);
    if(pasynInterface)
    {
        pasynInt32 = static_cast<asynInt32*>(pasynInterface->pinterface);
        pvtInt32 = pasynInterface->drvPvt;
        pasynUser->reason = ASYN_REASON_SIGNAL; // required for GPIB
        if (pasynInt32->registerInterruptUser(pvtInt32, pasynUser,
            intrCallbackInt32, this, &intrPvtInt32) == asynSuccess)
        {
            return true;
        }
        error("%s: bus %s does not allow to register for "
            "Int32 interrupts: %s\n",
            clientName(), busname, pasynUser->errorMessage);
        pasynInt32 = NULL;
        intrPvtInt32 = NULL;
    }

    // no asynInt32 available, thus try asynUInt32
    pasynInterface = pasynManager->findInterface(pasynUser,
        asynUInt32DigitalType, true);
    if(pasynInterface)
    {
        pasynUInt32 =
            static_cast<asynUInt32Digital*>(pasynInterface->pinterface);
        pvtUInt32 = pasynInterface->drvPvt;
        pasynUser->reason = ASYN_REASON_SIGNAL;
        if (pasynUInt32->registerInterruptUser(pvtUInt32,
            pasynUser, intrCallbackUInt32, this, 0xFFFFFFFF,
            &intrPvtUInt32) == asynSuccess)
        {
            return true;
        }
        error("%s: bus %s does not allow to register for "
            "UInt32 interrupts: %s\n",
            clientName(), busname, pasynUser->errorMessage);
        pasynUInt32 = NULL;
        intrPvtUInt32 = NULL;
    }

    // no event interface available, never mind
    return true;
}

// interface function: we want exclusive access to the device
// lockTimeout_ms=0 means "block here" (used in @init)
bool AsynDriverInterface::
lockRequest(unsigned long lockTimeout_ms)
{
    debug("AsynDriverInterface::lockRequest(%s, %ld msec)\n",
        clientName(), lockTimeout_ms);
    asynStatus status;
    lockTimeout = lockTimeout_ms ? lockTimeout_ms*0.001 : -1.0;
    if (!lockTimeout_ms)
    {
        if (!connectToAsynPort()) return false;
    }
    ioAction = Lock;
    status = pasynManager->queueRequest(pasynUser, priority(),
        lockTimeout);
    if (status != asynSuccess)
    {
        error("%s lockRequest: pasynManager->queueRequest() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    // continues with handleRequest() or handleTimeout()
    return true;
}

bool AsynDriverInterface::
connectToAsynPort()
{
    asynStatus status;
    int connected;

    debug("AsynDriverInterface::connectToAsynPort(%s)\n",
        clientName());
    status = pasynManager->isConnected(pasynUser, &connected);
    if (status != asynSuccess)
    {
        error("%s: pasynManager->isConnected() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    if (!connected)
    {
        status = pasynCommon->connect(pvtCommon, pasynUser);
        debug("AsynDriverInterface::connectToAsynPort(%s): "
                "status=%d\n",
            clientName(), status);
        if (status != asynSuccess)
        {
            error("%s: pasynCommon->connect() failed: %s\n",
                clientName(), pasynUser->errorMessage);
            return false;
        }
    }
    return true;
}

// now, we can have exclusive access (called by asynManager)
void AsynDriverInterface::
lockHandler()
{
    debug("AsynDriverInterface::lockHandler(%s)\n",
        clientName());
    pasynManager->blockProcessCallback(pasynUser, false);
    lockCallback(ioSuccess);
}

// interface function: we don't need exclusive access any more
bool AsynDriverInterface::
unlock()
{
    debug("AsynDriverInterface::unlock(%s)\n",
        clientName());
    pasynManager->unblockProcessCallback(pasynUser, false);
    return true;
}

// interface function: we want to write something
bool AsynDriverInterface::
writeRequest(const void* output, size_t size,
    unsigned long writeTimeout_ms)
{
#ifndef NO_TEMPORARY
    debug("AsynDriverInterface::writeRequest(%s, \"%s\", %ld msec)\n",
        clientName(), StreamBuffer(output, size).expand()(),
        writeTimeout_ms);
#endif

    asynStatus status;
    outputBuffer = (char*)output;
    outputSize = size;
    writeTimeout = writeTimeout_ms*0.001;
    ioAction = Write;
    status = pasynManager->queueRequest(pasynUser, priority(),
        writeTimeout);
    if (status != asynSuccess)
    {
        error("%s writeRequest: pasynManager->queueRequest() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    // continues with handleRequest() or handleTimeout()
    return true;
}

// now, we can write (called by asynManager)
void AsynDriverInterface::
writeHandler()
{
    debug("AsynDriverInterface::writeHandler(%s)\n",
        clientName());
    asynStatus status;
    size_t written = 0;
    pasynUser->timeout = writeTimeout;

    // discard any early input or early events
    status = pasynOctet->flush(pvtOctet, pasynUser);
    receivedEvent = 0;

    if (status != asynSuccess)
    {
        error("%s: pasynOctet->flush() failed: %s\n",
                clientName(), pasynUser->errorMessage);
        writeCallback(ioFault);
        return;
    }
    status = pasynOctet->writeRaw(pvtOctet, pasynUser,
        outputBuffer, outputSize, &written);
    switch (status)
    {
        case asynSuccess:
            outputBuffer += written;
            outputSize -= written;
            if (outputSize > 0)
            {
                status = pasynManager->queueRequest(pasynUser,
                    priority(), lockTimeout);
                if (status != asynSuccess)
                {
                    error("%s writeHandler: "
                        "pasynManager->queueRequest() failed: %s\n",
                        clientName(), pasynUser->errorMessage);
                    writeCallback(ioFault);
                }
                // continues with handleRequest() or handleTimeout()
                return;
            }
            writeCallback(ioSuccess);
            return;
        case asynTimeout:
            writeCallback(ioTimeout);
            return;
        case asynOverflow:
            error("%s: asynOverflow: %s\n",
                clientName(), pasynUser->errorMessage);
            writeCallback(ioFault);
            return;
        case asynError:
            error("%s: asynError: %s\n",
                clientName(), pasynUser->errorMessage);
            writeCallback(ioFault);
            return;
    }
}

// interface function: we want to read something
bool AsynDriverInterface::
readRequest(unsigned long replyTimeout_ms, unsigned long readTimeout_ms,
    long _expectedLength, bool async)
{
    asynStatus status;
    debug("AsynDriverInterface::readRequest(%s, %ld msec reply, "
        "%ld msec read, expect %ld bytes, asyn=%s)\n",
        clientName(), replyTimeout_ms, readTimeout_ms,
        _expectedLength, async?"yes":"no");
    readTimeout = readTimeout_ms*0.001;
    replyTimeout = replyTimeout_ms*0.001;
    double queueTimeout;
    expectedLength = _expectedLength;
    if (async)
    {
        ioAction = AsyncRead;
        queueTimeout = 0.0;
        // first poll for input,
        // later poll periodically if no other input arrives
        // from intrCallbackOctet()
    }
    else {
        ioAction = Read;
        queueTimeout = replyTimeout;
    }
    status = pasynManager->queueRequest(pasynUser,
        priority(), queueTimeout);
    if (status != asynSuccess && !async)
    {
        error("%s readRequest: pasynManager->queueRequest() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    // continues with handleRequest() or handleTimeout()
    return true;
}

// now, we can read (called by asynManager)
void AsynDriverInterface::
readHandler()
{
#ifndef NO_TEMPORARY
    debug("AsynDriverInterface::readHandler(%s) eoslen=%d:%s\n",
        clientName(), eoslen, StreamBuffer(eos, eoslen).expand()());
#endif
    while (eoslen >= 0)
    {
        if (pasynOctet->setInputEos(pvtOctet, pasynUser,
            eos, eoslen) == asynSuccess) break;
        eos++; eoslen--;
        if (eoslen < 0)
        {
            error("%s: warning: pasynOctet->setInputEos() failed: %s\n",
                clientName(), pasynUser->errorMessage);
        }
    }

    bool async = (ioAction == AsyncRead);
    int bytesToRead = peeksize;
    long buffersize;
    
    if (expectedLength > 0)
    {
        buffersize = expectedLength;
        if (peeksize > 1)
        {
            /* we can't peek, try to read whole message */
            bytesToRead = expectedLength;
        }
    }
    else
    {
        buffersize = inputBuffer.capacity()-1;
    }
    char* buffer = inputBuffer.clear().reserve(buffersize);
        
    pasynUser->timeout = async ? 0.0 : replyTimeout;
    ioAction = Read;
    bool waitForReply = true;
    size_t received;
    int eomReason;
    asynStatus status;
    long readMore;

    while (1)
    {
        readMore = 0;
        received = 0;
        eomReason = 0;
        debug("AsynDriverInterface::readHandler(%s): "
                "read(..., bytesToRead=%d, ...) timeout=%f seconds\n",
                clientName(), bytesToRead, pasynUser->timeout);
        status = pasynOctet->read(pvtOctet, pasynUser,
            buffer, bytesToRead, &received, &eomReason);

#ifndef NO_TEMPORARY
        debug("AsynDriverInterface::readHandler(%s): "
                "received %d of %d bytes \"%s\" status=%d eomReason=%d\n",
            clientName(), received, bytesToRead,
            StreamBuffer(buffer, received).expand()(),
            status, eomReason);
#endif

        switch (status)
        {
            case asynSuccess:
                readMore = readCallback(
                    eomReason & (ASYN_EOM_END|ASYN_EOM_EOS) ?
                    ioEnd : ioSuccess,
                    buffer, received);
                break;
            case asynTimeout:
                if (received == 0 && waitForReply)
                {
                    // reply timeout
                    if (async)
                    {
                        debug("AsynDriverInterface::readHandler(%s): "
                            "no async input, start timer %f seconds\n",
                            clientName(), replyTimeout);
                        // start next poll after timer expires
                        ioAction = AsyncRead;
                        if (replyTimeout != 0.0) startTimer(replyTimeout);
                        return;
                    }
                    debug("AsynDriverInterface::readHandler(%s): "
                        "no reply\n",
                        clientName());
                    readMore = readCallback(ioNoReply);
                    break;
                }
                // read timeout
                readMore = readCallback(ioTimeout, buffer, received);
                break;
            case asynOverflow:
                if (bytesToRead == 1)
                {
                    // device does not support peeking
                    // try to read whole message next time
                    inputBuffer.clear().reserve(100);
                } else {
                    // buffer was still too small
                    // try larger buffer next time
                    inputBuffer.clear().reserve(inputBuffer.capacity()*2);
                }
                peeksize = inputBuffer.capacity();
                // deliver whatever we have
                error("%s: asynOverflow: %s\n",
                    clientName(), pasynUser->errorMessage);
                readCallback(ioFault, buffer, received);
                break;
            case asynError:
                error("%s: asynError: %s\n",
                    clientName(), pasynUser->errorMessage);
                readCallback(ioFault, buffer, received);
                break;
        }
        if (!readMore) break;
        if (readMore > 0)
        {
            bytesToRead = readMore;
        }
        else
        {
            bytesToRead = inputBuffer.capacity()-1;
        }
        debug("AsynDriverInterface::readHandler(%s) "
            "readMore=%ld bytesToRead=%d\n",
            clientName(), readMore, bytesToRead);
        pasynUser->timeout = readTimeout;
        waitForReply = false;
    }
}

void intrCallbackOctet(void* /*pvt*/, asynUser *pasynUser,
    char *data, size_t numchars, int /*eomReason*/)
{
    // we must be very careful not to block in this function
    // we must not call cancelRequest from here!

    AsynDriverInterface* interface =
        static_cast<AsynDriverInterface*>(pasynUser->userPvt);
    if (interface->ioAction == AsyncRead ||
        interface->ioAction == AsyncReadMore)
    {
    // cancel possible readTimeout or poll timer
        interface->ioAction = AsyncReadCancelled;
        interface->cancelTimer();
    
    // cancel possible read poll
        interface->asynReadHandler(data, numchars);
    }
}

// get asynchronous input
void AsynDriverInterface::
asynReadHandler(char *buffer, size_t received)
{
#ifndef NO_TEMPORARY
    debug("AsynDriverInterface::asynReadHandler(%s, buffer=\"%s\", "
            "received=%d)\n",
        clientName(), StreamBuffer(buffer, received).expand()(),
        received);
#endif

    long readMore = 1;
    if (received)
    {
        // process what we got and look if we need more data
        readMore = readCallback(ioSuccess, buffer, received);
    }
    if (readMore)
    {
        // wait for more input
        ioAction = AsyncReadMore;
        startTimer(readTimeout);
    }
    else
    {
        // start next poll after timer expires
        ioAction = AsyncRead;
        startTimer(replyTimeout);
    }
}

// interface function: we want to receive an event
bool AsynDriverInterface::
acceptEvent(unsigned long mask, unsigned long replytimeout_ms)
{
    if (receivedEvent & mask)
    {
        // handle early events
        receivedEvent = 0;
        eventCallback(ioSuccess);
        return true;
    }
    eventMask = mask;
    ioAction = ReceiveEvent;
    startTimer(replytimeout_ms*0.001);
    return true;
}

void intrCallbackInt32(void* /*pvt*/, asynUser *pasynUser, epicsInt32 data)
{
    AsynDriverInterface* interface =
        static_cast<AsynDriverInterface*>(pasynUser->userPvt);
    debug("AsynDriverInterface::intrCallbackInt32 (%s, %ld)\n",
        interface->clientName(), (long int) data);
    if (interface->eventMask)
    {
        if (data & interface->eventMask)
        {
            interface->eventMask = 0;
            interface->eventCallback(AsynDriverInterface::ioSuccess);
        }
        return;
    }
    // store early events
    interface->receivedEvent = data;
}

void intrCallbackUInt32(void* /*pvt*/, asynUser *pasynUser,
    epicsUInt32 data)
{
    AsynDriverInterface* interface =
        static_cast<AsynDriverInterface*>(pasynUser->userPvt);
    debug("AsynDriverInterface::intrCallbackUInt32 (%s, %ld)\n",
        interface->clientName(), (long int) data);
    if (interface->eventMask)
    {
        if (data & interface->eventMask)
        {
            interface->eventMask = 0;
            interface->eventCallback(AsynDriverInterface::ioSuccess);
        }
        return;
    }
    // store early events
    interface->receivedEvent = data;
}

void AsynDriverInterface::
timerExpired()
{
    int autoconnect, connected;
    debug("AsynDriverInterface::timerExpired(%s)\n", clientName());
    switch (ioAction)
    {
        case ReceiveEvent:
            // timeout while waiting for event
            ioAction = None;
            eventCallback(ioTimeout);
            return;
        case AsyncReadMore:
            // timeout after reading some async data
            readCallback(ioTimeout);
            ioAction = AsyncRead;
            startTimer(replyTimeout);
            return;
        case AsyncRead:
            // no async input for some time, thus let's poll
            // queueRequest might fail if another request just queued
            pasynManager->isAutoConnect(pasynUser, &autoconnect);
            pasynManager->isConnected(pasynUser, &connected);
            if (autoconnect && !connected)
            {
                // has explicitely been disconnected
                // a poll would autoConnect which is not what we want
                startTimer(replyTimeout);
            }
            else
            {
                // queue for read poll
                pasynManager->queueRequest(pasynUser,
                    asynQueuePriorityLow, replyTimeout);
                // continues with handleRequest() or handleTimeout()
            }
            return;
        case AsyncReadCancelled:
            // already got input but couldn't cancel timer quick enough
            return;
        case Read:
            // No idea why this happens
            return;
        default:
            error("INTERNAL ERROR (%s): timerExpired() unexpected ioAction %s\n",
                clientName(), ioActionStr[ioAction]);
            return;
    }
}

#ifdef EPICS_3_14
epicsTimerNotify::expireStatus AsynDriverInterface::
expire(const epicsTime &)
{
    timerExpired();
    return noRestart;
}
#else
void AsynDriverInterface::
expire(CALLBACK *pcallback)
{
    AsynDriverInterface* interface =
        static_cast<AsynDriverInterface*>(pcallback->user);
    interface->timerExpired();
}
#endif

bool AsynDriverInterface::
connectRequest(unsigned long connecttimeout_ms)
{
    double queueTimeout = connecttimeout_ms*0.001;
    asynStatus status;
    ioAction = Connect;
    status = pasynManager->queueRequest(pasynUser,
        asynQueuePriorityConnect, queueTimeout);
    if (status != asynSuccess)
    {
        error("%s connectRequest: pasynManager->queueRequest() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    // continues with handleRequest() or handleTimeout()
    return true;
}

void AsynDriverInterface::
connectHandler()
{
    asynStatus status;
    status = pasynCommon->connect(pvtCommon, pasynUser);
    if (status != asynSuccess)
    {
        error("%s connectRequest: pasynCommon->connect() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        connectCallback(ioFault);
        return;
    }
    connectCallback(ioSuccess);
}

bool AsynDriverInterface::
disconnect()
{
    asynStatus status;
    ioAction = Disconnect;
    status = pasynManager->queueRequest(pasynUser,
        asynQueuePriorityConnect, 0.0);
    if (status != asynSuccess)
    {
        error("%s disconnect: pasynManager->queueRequest() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return false;
    }
    // continues with handleRequest() or handleTimeout()
    return true;
}

void AsynDriverInterface::
disconnectHandler()
{
    asynStatus status;
    status = pasynCommon->disconnect(pvtCommon, pasynUser);
    if (status != asynSuccess)
    {
        error("%s connectRequest: pasynCommon->disconnect() failed: %s\n",
            clientName(), pasynUser->errorMessage);
        return;
    }
}

void AsynDriverInterface::
cancelAll()
{
    cancelTimer();
    if (pasynOctet)
    {
        // octet stream interface is connected
        int wasQueued;
        pasynManager->cancelRequest(pasynUser, &wasQueued);
        // does not return until running handler has finished
    }
}

// asynUser callbacks to pasynManager->queueRequest()

void handleRequest(asynUser* pasynUser)
{
    AsynDriverInterface* interface =
        static_cast<AsynDriverInterface*>(pasynUser->userPvt);
    debug("AsynDriverInterface::handleRequest(%s) %s\n",
        interface->clientName(), ioActionStr[interface->ioAction]);
    switch (interface->ioAction)
    {
        case Lock:
            interface->lockHandler();
            break;
        case Write:
            interface->writeHandler();
            break;
        case AsyncRead: // polled async input
            interface->readHandler();
            break;
        case AsyncReadCancelled: // already got input, ignore request
            break;
        case Read:      // sync input
            interface->readHandler();
            break;
        case Connect:
            interface->connectHandler();
            break;
        case Disconnect:
            interface->disconnectHandler();
            break;
        default:
            error("INTERNAL ERROR (%s): "
                "handleRequest() unexpected ioAction %s\n",
                interface->clientName(), ioActionStr[interface->ioAction]);
    }
}

void handleTimeout(asynUser* pasynUser)
{
    AsynDriverInterface* interface =
        static_cast<AsynDriverInterface*>(pasynUser->userPvt);
    debug("AsynDriverInterface::handleTimeout(%s) %s\n",
        interface->clientName(), ioActionStr[interface->ioAction]);
    switch (interface->ioAction)
    {
        case Lock:
            interface->lockCallback(AsynDriverInterface::ioTimeout);
            break;
        case Write:
            interface->writeCallback(AsynDriverInterface::ioTimeout);
            break;
        case Read:
            interface->readCallback(AsynDriverInterface::ioFault, NULL, 0);
            break;
        case AsyncRead: // async poll failed, try later
            interface->startTimer(interface->replyTimeout);
            break;
        case AsyncReadCancelled: // already got input, ignore timeout
            break;
        case Connect:
            interface->connectCallback(AsynDriverInterface::ioTimeout);
            break;
        case Disconnect:
            // not interested in callback
            break;
        default:
            error("INTERNAL ERROR (%s): handleTimeout() "
                "unexpected ioAction %s\n",
                interface->clientName(), ioActionStr[interface->ioAction]);
    }
}

