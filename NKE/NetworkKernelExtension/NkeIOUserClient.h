/* 
 * Copyright (c) 2010 Slava Imameev. All rights reserved.
 */

#ifndef _NKEIOUSERCLIENT_H
#define _NKEIOUSERCLIENT_H

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IODataQueue.h>
#include <sys/types.h>
#include <sys/kauth.h>
#include "NkeCommon.h"
#include "NkeIOUserClientRef.h"
#include "NkeUserToKernel.h"
#include "NetworkKernelExtension.h"

//--------------------------------------------------------------------

class com_devicelock_driver_DeviceLockIOKitDriver;

//--------------------------------------------------------------------

class NkeIOUserClient : public IOUserClient
{
    OSDeclareDefaultStructors( NkeIOUserClient )
    
private:
    task_t                           fClient;
    proc_t                           fClientProc;
    pid_t                            fClientPID;
    
    //
    // data queues for kernel-user data transfer
    //
    IODataQueue*                     fDataQueue[ kt_NkeNotifyTypeMax ];
    
    //
    // memory descriptors for the queues
    //
    IOMemoryDescriptor*              fSharedMemory[ kt_NkeNotifyTypeMax ];
    
    //
    // mach port for notifications that the data is available in he queues
    //
    mach_port_t                      fNotificationPorts[ kt_NkeNotifyTypeMax ];
    
    //
    // locks to serialize access to the queues
    //
    IOLock*                          fLock[ kt_NkeNotifyTypeMax ];
    
    //
    // size of the queues, must be initialized before creating queues
    //
    UInt32                           fQueueSize[ kt_NkeNotifyTypeMax ];
    //kauth_listener_t                 fListener;
    
    //
    // true if a user client calls kt_NkeUserClientClose operation
    //
    Boolean                          clientClosedItself;
    
    //
    // an object to which this client is attached
    //
    NetworkKernelExtension *fProvider;
    
protected:
    
    virtual void freeAllocatedResources();
    
    virtual void free();
    
public:
    virtual bool     start( __in IOService *provider );
    virtual void     stop( __in IOService *provider );
    virtual IOReturn open(void);
    virtual IOReturn clientClose(void);
    virtual IOReturn close(void);
    virtual bool     terminate(IOOptionBits options);
    virtual IOReturn startLogging(void);
    virtual IOReturn stopLogging(void);
    
    virtual pid_t getPid(){ return this->fClientPID; }
    virtual bool isClientProc( __in proc_t proc ){ assert(this->fClientProc); return ( proc == this->fClientProc ); };
    
    virtual IOReturn registerNotificationPort( mach_port_t port, UInt32 type, UInt32 refCon);
    
    virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
                                         IOMemoryDescriptor **memory);
    
    virtual IOReturn socketFilterNotification( __in NkeSocketFilterNotification* data );
    
    virtual IOReturn processServiceSocketFilterResponse( __in  void *vInBuffer, // NkeSocketFilterServiceResponse
                                                         __out void *vOutBuffer,
                                                         __in  void *vInSize,
                                                         __in  void *vOutSizeP,
                                                        void *, void *);
    
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
                                                         UInt32 index);
    
    //
    // alocates a memory in the kernel mode and copies a content of a user mode memory, the allocated memory is of
    // the same size as provided by the second parameter, the memory is allocated by a call to IOMalloc, a caller must free
    //
    virtual void* userModeMemoryToKernelMode( __in mach_vm_address_t  userAddress, __in mach_vm_size_t bufferSize );
    
    static NkeIOUserClient* withTask( __in task_t owningTask );
    
};

//--------------------------------------------------------------------

#endif//_NKEIOUSERCLIENT_H