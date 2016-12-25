//
//  NkeConnection.h
//  NkeClient
//
//  Created by slava on 25/12/2016.
//  Copyright (c) 2016 Slava-Imameev. All rights reserved.
//

#ifndef __NkeClient__NkeConnection__
#define __NkeClient__NkeConnection__

#include <iostream>

#include <IOKit/IOKitLib.h>
#include <IOKit/IODataQueueShared.h>
#include <IOKit/IODataQueueClient.h>

#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/acl.h>

#include "../../NKE/NetworkKernelExtension/NkeUserToKernel.h"

kern_return_t
NkeOpenDlDriver(
                io_connect_t*    connection
                );

#endif /* defined(__NkeClient__NkeConnection__) */
