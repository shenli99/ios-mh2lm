/*
 * Copyright (c) 1998-2012 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * NOTICE: This file was modified by SPARTA, Inc. in 2005 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 */

const char * gIOKernelConfigTables =
    "("
#if defined(ARM64_BOARD_CONFIG_SWEET)
    "   {"
    "     'IOClass'         = GenericInterruptController;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'arm,gic-v3';"
    "   },"
#if defined(ARM64_BOARD_CONFIG_MH2LM)
    "   {"
    "     'IOClass'         = SweetTouchPanel;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'sweet,touch-panel';"
    "   },"
    "   {"
    "     'IOClass'         = SweetPanelPins;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'sweet,panel-pins';"
    "   },"
    "   {"
    "     'IOClass'         = SweetRPMh;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'sweet,rpmh';"
    "   },"
    "   {"
    "     'IOClass'         = SweetMDSS;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'sweet,mdss';"
    "   },"
    "   {"
    "     'IOClass'         = SweetSpmiInterruptMux;"
    "     'IOProviderClass' = SweetSpmiMuxDevice;"
    "     'IOMatchCategory' = SweetSpmiInterruptMux;"
    "   },"
    "   {"
    "     'IOClass'         = SweetPmicAdc5;"
    "     'IOProviderClass' = SweetPmicAdc5Device;"
    "     'IOMatchCategory' = SweetPmicAdc5;"
    "   },"
    "   {"
    "     'IOClass'         = SweetPmicKeys;"
    "     'IOProviderClass' = SweetPmicKeysDevice;"
    "     'IOMatchCategory' = SweetPmicKeys;"
    "   },"
    "   {"
    "     'IOClass'         = SweetPlatformExpert;"
    "     'IOProviderClass' = IOPlatformExpertDevice;"
    "     'IONameMatch'     = 'lge,mh2lm';"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
    "   {"
    "     'IOClass'         = SweetCPU;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'qcom,kryo485';"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
#elif defined(ARM64_BOARD_CONFIG_GUACAMOLE)
    "   {"
    "     'IOClass'         = SweetPlatformExpert;"
    "     'IOProviderClass' = IOPlatformExpertDevice;"
    "     'IONameMatch'     = 'oneplus,guacamole';"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
    "   {"
    "     'IOClass'         = SweetCPU;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'qcom,kryo485';"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
#else /* SWEET board */
    "   {"
    "     'IOClass'         = SweetPlatformExpert;"
    "     'IOProviderClass' = IOPlatformExpertDevice;"
    "     'IONameMatch'     = 'xiaomi,sweet';"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
    "   {"
    "     'IOClass'         = SweetCPU;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'qcom,kryo470';"
    "     'IOProbeScore'    = 10000:32;"
    "   },"
#endif /* ARM64_BOARD_CONFIG_GUACAMOLE */
    "   {"
    "     'IOClass'         = AppleKeyStore;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = AppleKeyStore;"
    "   },"
    "   {"
    "     'IOClass'         = IOWatchdog;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = IOWatchdog;"
    "   },"
    "   {"
    "     'IOClass'         = IOCoreSurfaceRoot;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = IOCoreSurfaceRoot;"
    "   },"
    "   {"
    "     'IOClass'         = AppleCLCD;"
    "     'IOProviderClass' = IOPlatformDevice;"
    "     'IONameMatch'     = 'sweet,display';"
    "   },"
    "   {"
    "     'IOClass'         = SweetHIDButtons;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = SweetHIDButtons;"
    "   },"
    "   {"
    "     'IOClass'         = SweetHIDTouch;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = SweetHIDTouch;"
    "   },"
    "   {"
    "     'IOClass'         = AppleSmartBatteryManager;"
    "     'IOProviderClass' = IOResources;"
    "     'IOMatchCategory' = AppleSmartBatteryManager;"
    "   },"
#endif
    "   {"
    "     'IOClass'         = IOPanicPlatform;"
    "     'IOProviderClass' = IOPlatformExpertDevice;"
    "     'IOProbeScore'    = 0:32;"
    "   }"
    ")";
