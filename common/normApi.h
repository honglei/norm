#ifndef _NORM_API
#define _NORM_API

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif // WIN32

////////////////////////////////////////////////////////////
// IMPORTANT NOTICE
//  The NORM API is _very_ much in a developmental phase
//  right now.  So, although this code is available in
//  source code distribution, it is very much subject
//  to change in future revisions.  The goal of the NORM
//  API _will_ be to provide a stable base of function calls
//  for applications to use, even as the underlying NORM
//  C++ code continues to evolve.  But, until this notice
//  is removed, the API shouldn't be considered final.


typedef const void* NormInstanceHandle;
extern const NormInstanceHandle NORM_INSTANCE_INVALID;

NormInstanceHandle NormCreateInstance();
void NormDestroyInstance(NormInstanceHandle instanceHandle);

// NORM session creation and control
typedef const void* NormSessionHandle;
extern const NormSessionHandle NORM_SESSION_INVALID;
typedef unsigned long NormNodeId;
extern const NormNodeId NORM_NODE_NONE;
extern const NormNodeId NORM_NODE_ANY;

NormSessionHandle NormCreateSession(NormInstanceHandle instanceHandle,
                                    const char*        sessionAddress,
                                    unsigned short     sessionPort,
                                    NormNodeId         localNodeId);

void NormDestroySession(NormSessionHandle sessionHandle);

// Session management and parameters

void NormSetTransmitRate(NormSessionHandle sessionHandle,
                         double            bitsPerSecond);

void NormSetGrttEstimate(NormSessionHandle sessionHandle,
                         double            grttEstimate);

NormNodeId NormGetLocalNodeId(NormSessionHandle sessionHandle);

bool NormSetLoopback(NormSessionHandle sessionHandle, bool state);

// Debug parameters
void NormSetMessageTrace(NormSessionHandle sessionHandle, bool state);
void NormSetTxLoss(NormSessionHandle sessionHandle, double percent);
void NormSetRxLoss(NormSessionHandle sessionHandle, double percent);

// Sender control & parameters
bool NormStartSender(NormSessionHandle  sessionHandle,
                     unsigned long      bufferSpace,
                     unsigned short     segmentSize,
                     unsigned char      numData,
                     unsigned char      numParity);

void NormStopSender(NormSessionHandle sessionHandle);

bool NormAddAckingNode(NormSessionHandle  sessionHandle,
                       NormNodeId         nodeId);

void NormRemoveAckingNode(NormSessionHandle  sessionHandle,
                          NormNodeId         nodeId);


// Receiver control & parameters
bool NormStartReceiver(NormSessionHandle  sessionHandle,
                       unsigned long      bufferSpace);

void NormStopReceiver(NormSessionHandle  sessionHandle);

void NormSetDefaultUnicastNack(NormSessionHandle sessionHandle,
                               bool              state);

typedef const void* NormNodeHandle;
extern const NormNodeHandle NORM_NODE_INVALID;
typedef const void* NormObjectHandle;
extern const NormObjectHandle NORM_OBJECT_INVALID;
typedef unsigned short NormObjectTransportId;

enum NormNackingMode
{
    NORM_NACK_NONE,
    NORM_NACK_INFO_ONLY,
    NORM_NACK_NORMAL  
};
    
void NormSetDefaultNackingMode(NormSessionHandle sessionHandle,
                               NormNackingMode   nackingMode);
    
enum NormRepairBoundary
{
    NORM_BOUNDARY_BLOCK,
    NORM_BOUNDARY_OBJECT
};
    
void NormSetDefaultRepairBoundary(NormSessionHandle  sessionHandle,
                                  NormRepairBoundary repairBoundary); 

NormRepairBoundary NormNodeGetRepairBoundary(NormNodeHandle nodeHandle);

void NormNodeSetRepairBoundary(NormNodeHandle     nodeHandle,
                               NormRepairBoundary repairBoundary);

NormNodeId NormNodeGetId(NormNodeHandle nodeHandle);

void NormNodeSetNackingMode(NormNodeHandle   nodeHandle,
                            NormNackingMode  nackingMode);

                                          
// General NormObject functions

enum NormObjectType
{
    NORM_OBJECT_NONE,
    NORM_OBJECT_DATA,
    NORM_OBJECT_FILE,
    NORM_OBJECT_STREAM   
};
    
    
NormObjectType NormObjectGetType(NormObjectHandle objectHandle);


bool NormObjectGetInfo(NormObjectHandle   objectHandle,
                       char*              infoBuffer,
                       unsigned short*    infoLen);

NormObjectTransportId NormObjectGetTransportId(NormObjectHandle objectHandle);

void NormObjectCancel(NormObjectHandle objectHandle);


void NormObjectReNormObjectRetaintain(NormObjectHandle objectHandle);
void NormObjectRelease(NormObjectHandle objectHandle);


// Receiver-only NormObject functions
NormNackingMode NormObjectGetNackingMode(NormObjectHandle objectHandle);
        
void NormObjectSetNackingMode(NormObjectHandle objectHandle,
                              NormNackingMode  nackingMode);




// Sender-only NormObject functions
bool NormSetWatermark(NormSessionHandle  sessionHandle,
                      NormObjectHandle   objectHandle);

        
// NormStreamObject functions

NormObjectHandle NormStreamOpen(NormSessionHandle sessionHandle,
                                unsigned long     bufferSize);

void NormStreamClose(NormObjectHandle streamHandle);

enum NormFlushMode
{
    NORM_FLUSH_NONE,
    NORM_FLUSH_PASSIVE,
    NORM_FLUSH_ACTIVE   
};

void NormStreamSetFlushMode(NormObjectHandle    streamHandle,
                            NormFlushMode       flushMode);

void NormStreamSetPushMode(NormObjectHandle streamHandle, 
                           bool             state);

unsigned int NormStreamWrite(NormObjectHandle   streamHandle,
                             const char*        buffer,
                             unsigned int       numBytes);

void NormStreamFlush(NormObjectHandle streamHandle, bool eom = false);

void NormStreamMarkEom(NormObjectHandle streamHandle);

bool NormStreamRead(NormObjectHandle   streamHandle,
                    char*              buffer,
                    unsigned int*      numBytes);

bool NormStreamSeekMsgStart(NormObjectHandle streamHandle);

// NormFileObject Functions
NormObjectHandle NormFileEnqueue(NormSessionHandle sessionHandle,
                                 const char*  fileName,
                                 const char*  infoPtr = (const char*)0, 
                                 unsigned int infoLen = 0);

bool NormSetCacheDirectory(NormInstanceHandle instanceHandle, 
                           const char*        cachePath);

bool NormFileGetName(NormObjectHandle   fileHandle,
                     char*              nameBuffer,
                     unsigned int       bufferLen);

bool NormFileRename(NormObjectHandle   fileHandle,
                    const char*        fileName);

// NormDataObject Functions
bool NormDataEnqueue(const char*   dataPtr,
                     unsigned long dataLen,
                     const char*   infoPtr = (const char*)0, 
                     unsigned int  infoLen = 0);

// NORM Event Notification Routines

enum NormEventType
{
    NORM_EVENT_INVALID = 0,
    NORM_TX_QUEUE_VACANCY,
    NORM_TX_QUEUE_EMPTY,
    NORM_TX_OBJECT_SENT,
    NORM_TX_OBJECT_PURGED,
    NORM_LOCAL_SERVER_CLOSED,
    NORM_REMOTE_SERVER_NEW,
    NORM_REMOTE_SERVER_INACTIVE,
    NORM_REMOTE_SERVER_ACTIVE,
    NORM_RX_OBJECT_NEW,
    NORM_RX_OBJECT_INFO,
    NORM_RX_OBJECT_UPDATE,
    NORM_RX_OBJECT_COMPLETED,
    NORM_RX_OBJECT_ABORTED
};

typedef struct
{
    NormEventType       type;
    NormSessionHandle   session;
    NormNodeHandle      sender;
    NormObjectHandle    object;
} NormEvent;


// This call blocks until the next NormEvent is ready unless asynchronous
// notification is used (see below)
bool NormGetNextEvent(NormInstanceHandle instanceHandle, NormEvent* theEvent);

// The "NormGetDescriptor()" function returns a HANDLE (WIN32) or
// a file descriptor (UNIX) which can be used for async notification
// of pending NORM events. On WIN32, the returned HANDLE can be used 
// with system calls like "WaitForSingleEvent()", and on Unix the
// returned descriptor can be used in a "select()" call.  If this type
// of asynchronous notification is used, calls to "NormGetNextEvent()" will
// not block.

#ifdef WIN32
typedef HANDLE NormDescriptor;
#else
typedef int NormDescriptor;
#endif // if/else WIN32/UNIX
NormDescriptor NormGetDescriptor(NormInstanceHandle instanceHandle);

#endif // _NORM_API
