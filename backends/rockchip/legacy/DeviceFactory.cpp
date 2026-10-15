#include "DeviceFactory.h"

CRKDevice *CDeviceFactory::CreateDevice(STRUCT_RKDEVICE_DESC &device)
{
	return new CRKAndroidDevice(device);
}

void CDeviceFactory::FreeDevice(CRKDevice *pDevice)
{
	if (pDevice)
		delete pDevice;
}
