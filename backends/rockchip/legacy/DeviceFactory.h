#ifndef DEVICEFACTORY_HEADER
#define DEVICEFACTORY_HEADER

#include "AndroidDevice.h"

// CreateDevice always builds a CRKAndroidDevice; FreeDevice deletes through the
// virtual destructor.
class CDeviceFactory
{
public:
	static CRKDevice *CreateDevice(STRUCT_RKDEVICE_DESC &device);
	static void FreeDevice(CRKDevice *pDevice);
};

#endif
