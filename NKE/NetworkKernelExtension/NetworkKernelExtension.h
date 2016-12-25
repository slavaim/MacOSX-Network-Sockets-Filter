/*
 * Copyright (c) 2016 Slava Imameev. All rights reserved.
 */

#ifndef __NETWORKKERNELEXTENSION_H__
#define __NETWORKKERNELEXTENSION_H__

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>

#include "NkeCommon.h"

//--------------------------------------------------------------------

//
// the I/O Kit driver class
//
class NetworkKernelExtension : public IOService
{
    OSDeclareDefaultStructors(NetworkKernelExtension)
    
public:
    virtual bool start(IOService *provider);
    virtual void stop( IOService * provider );
    
    virtual IOReturn newUserClient( __in task_t owningTask,
                                    __in void*,
                                    __in UInt32 type,
                                    __in IOUserClient **handler );

    static NetworkKernelExtension*  getInstance(){ return NetworkKernelExtension::Instance; };
    
protected:
    
    virtual bool init();
    virtual void free();
    
private:
    
    static NetworkKernelExtension* Instance;
    
};

//--------------------------------------------------------------------

#endif//__NETWORKKERNELEXTENSION_H__
