#include "backends/allwinner/Transport.h"
#include <libusb.h>
#include <libefex.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
namespace {
struct Wire {
    libusb_device_handle *handle = nullptr;
    libusb_context *context=nullptr;
    core::Cancellation cancel;
    bool cleanup = false;
    std::string error;
};
struct TransferResult { bool finished=false; libusb_transfer_status status=LIBUSB_TRANSFER_ERROR; int count=0; };
void LIBUSB_CALL Completed(libusb_transfer *transfer) {
    auto *r=static_cast<TransferResult *>(transfer->user_data);
    r->status=transfer->status; r->count=transfer->actual_length; r->finished=true;
}
int Bulk(void *opaque,int ep,char *data,ssize_t length) {
    auto *wire=static_cast<Wire *>(opaque);
    if(!wire || !wire->handle || length<0) return EFEX_ERR_INVALID_PARAM;
    for(ssize_t pos=0;pos<length;) {
        if(!wire->cleanup && wire->cancel && wire->cancel->stop_requested()) { wire->error="Cancelled"; return EFEX_ERR_USB_TRANSFER; }
        int amount=int(std::min<ssize_t>(length-pos,65536));
        auto *transfer=libusb_alloc_transfer(0); if(!transfer) return EFEX_ERR_MEMORY;
        TransferResult result;
        libusb_fill_bulk_transfer(transfer,wire->handle,uint8_t(ep),reinterpret_cast<unsigned char *>(data+pos),amount,Completed,&result,60000);
        int rc=libusb_submit_transfer(transfer);
        if(rc) { wire->error=libusb_error_name(rc); libusb_free_transfer(transfer); return EFEX_ERR_USB_TRANSFER; }
        bool cancelled=false;
        while(!result.finished) {
            if(!cancelled && !wire->cleanup && wire->cancel && wire->cancel->stop_requested()) { libusb_cancel_transfer(transfer); cancelled=true; }
            timeval timeout={0,50000}; libusb_handle_events_timeout(wire->context,&timeout);
        }
        libusb_free_transfer(transfer);
        if(result.status!=LIBUSB_TRANSFER_COMPLETED||result.count!=amount) {
            wire->error="USB transfer status="+std::to_string(result.status)+", bytes="+std::to_string(result.count)+"/"+std::to_string(amount);
            return EFEX_ERR_USB_TRANSFER;
        }
        pos+=amount;
    }
    return 0;
}
}
extern "C" int sunxi_usb_bulk_send(void *h,int ep,const char *b,ssize_t n) { return Bulk(h,ep,const_cast<char *>(b),n); }
extern "C" int sunxi_usb_bulk_recv(void *h,int ep,char *b,ssize_t n) { return Bulk(h,ep,b,n); }
namespace core { namespace allwinner {
namespace {
DeviceIdentity Identity(libusb_device *dev) {
    DeviceIdentity d; d.vendor=Vendor::Allwinner; d.bus=libusb_get_bus_number(dev);
    uint8_t ports[16]; int n=libusb_get_port_numbers(dev,ports,sizeof(ports));
    if(n>0) d.ports.assign(ports,ports+n);
    return d;
}
class Transport : public ITransport {
    std::shared_ptr<platform::UsbContext> usb;
    sunxi_efex_ctx_t ctx={}; Wire wire; int interfaceNumber=-1; bool detached=false;
    void Check(int rc) { if(rc) throw std::runtime_error(std::string(sunxi_efex_strerror(rc))+ (wire.error.empty()?"":": "+wire.error)); }
public:
    explicit Transport(std::shared_ptr<platform::UsbContext> context) : usb(std::move(context)) { wire.context=usb->get(); }
    ~Transport() override { Close(); }
    void Close() override {
        if(wire.handle) {
            if(interfaceNumber>=0) libusb_release_interface(wire.handle,interfaceNumber);
            if(detached) libusb_attach_kernel_driver(wire.handle,interfaceNumber);
            libusb_close(wire.handle);
        }
        wire.handle=nullptr; ctx={}; detached=false; interfaceNumber=-1;
    }
    void Open(const DeviceIdentity &target,const Cancellation &cancel) override {
        Close(); wire.cancel=cancel; wire.error.clear();
        if(target.vendor!=Vendor::Allwinner || target.ports.empty()) throw std::runtime_error("Missing full USB device identity");
        libusb_device **list=nullptr; ssize_t n=libusb_get_device_list(wire.context,&list);
        if(n<0) throw std::runtime_error(libusb_error_name(int(n)));
        int found=0; libusb_device *selected=nullptr;
        for(ssize_t i=0;i<n;++i) {
            libusb_device_descriptor desc; if(libusb_get_device_descriptor(list[i],&desc)) continue;
            if(desc.idVendor!=0x1f3a || desc.idProduct!=0xefe8) continue;
            auto d=Identity(list[i]); if(d.bus==target.bus && d.ports==target.ports) { ++found; selected=list[i]; }
        }
        int rc=0;
        if(found==1) rc=libusb_open(selected,&wire.handle);
        if(found!=1||rc) { libusb_free_device_list(list,1); throw std::runtime_error(found==0?"Target device disconnected":found>1?"Ambiguous device identity":libusb_error_name(rc)); }
        libusb_config_descriptor *config=nullptr;
        rc=libusb_get_active_config_descriptor(selected,&config);
        if(!rc) {
            for(unsigned i=0;i<config->bNumInterfaces && interfaceNumber<0;++i) {
                if(config->interface[i].num_altsetting < 1) continue;
                const auto &alt=config->interface[i].altsetting[0]; int in=0,out=0;
                for(unsigned j=0;j<alt.bNumEndpoints;++j) {
                    const auto &e=alt.endpoint[j]; if((e.bmAttributes&3)!=LIBUSB_TRANSFER_TYPE_BULK) continue;
                    if(e.bEndpointAddress&LIBUSB_ENDPOINT_IN) in=e.bEndpointAddress; else out=e.bEndpointAddress;
                }
                if(in&&out) { interfaceNumber=alt.bInterfaceNumber; ctx.epin=in; ctx.epout=out; }
            }
            libusb_free_config_descriptor(config);
        }
        libusb_free_device_list(list,1);
        if(interfaceNumber<0) throw std::runtime_error("No Allwinner USB bulk interface");
        if(libusb_kernel_driver_active(wire.handle,interfaceNumber)==1) {
            rc=libusb_detach_kernel_driver(wire.handle,interfaceNumber); if(rc) throw std::runtime_error(libusb_error_name(rc)); detached=true;
        }
        rc=libusb_claim_interface(wire.handle,interfaceNumber); if(rc) throw std::runtime_error(libusb_error_name(rc));
        ctx.hdl=&wire; Check(sunxi_efex_init(&ctx));
    }
    DeviceDetails Details() const override { DeviceDetails d; d.chip=ctx.resp.id; d.firmware=ctx.resp.firmware; d.mode=ctx.resp.mode; return d; }
    void FelWrite(uint32_t addr,const Bytes &b) override { Check(sunxi_efex_fel_write(&ctx,addr,reinterpret_cast<const char *>(b.data()),b.size())); }
    Bytes FelRead(uint32_t addr,size_t n) override { Bytes b(n); Check(sunxi_efex_fel_read(&ctx,addr,reinterpret_cast<char *>(b.data()),b.size())); return b; }
    void FelExec(uint32_t addr) override { Check(sunxi_efex_fel_exec(&ctx,addr)); }
    uint32_t QueryStorage() override { uint32_t v=0; Check(sunxi_efex_fes_query_storage(&ctx,&v)); return U32(reinterpret_cast<uint8_t *>(&v)); }
    uint32_t QuerySecure() override { uint32_t v=0; Check(sunxi_efex_fes_query_secure(&ctx,&v)); return U32(reinterpret_cast<uint8_t *>(&v)); }
    uint32_t QuerySize() override { uint32_t v=0; Check(sunxi_efex_fes_probe_flash_size(&ctx,&v)); return U32(reinterpret_cast<uint8_t *>(&v)); }
    void FlashAccess(bool on) override {
        uint32_t type=0; wire.cleanup=!on;
        int rc=sunxi_efex_fes_flash_set_onoff(&ctx,&type,on?1:0); wire.cleanup=false; Check(rc);
    }
    void Download(uint32_t addr,uint32_t tag,const Bytes &b) override { Check(sunxi_efex_fes_down(&ctx,reinterpret_cast<const char *>(b.data()),b.size(),addr,static_cast<sunxi_fes_data_type_t>(tag))); }
    VerifyResult Verify(uint32_t addr,uint64_t size,bool status) override {
        sunxi_fes_verify_resp_t r={}; Check(status?sunxi_efex_fes_verify_status(&ctx,addr,&r):sunxi_efex_fes_verify_value(&ctx,addr,size,&r));
        VerifyResult result; result.flag=U32(reinterpret_cast<const uint8_t *>(&r.flag)); result.checksum=U32(reinterpret_cast<const uint8_t *>(&r.media_crc)); return result;
    }
    void Finish(PostAction action) override {
        auto mode=action==PostAction::Reboot?TOOL_MODE_REBOOT:TOOL_MODE_POWEROFF;
        Check(sunxi_efex_fes_tool_mode(&ctx,TOOL_MODE_NORMAL,mode));
    }
};
}
std::unique_ptr<ITransport> UsbTransport(std::shared_ptr<platform::UsbContext> context) { return std::make_unique<Transport>(std::move(context)); }
std::vector<DeviceDescriptor> ScanUsb(libusb_context *context) {
    libusb_device **list=nullptr; ssize_t n=libusb_get_device_list(context,&list); std::vector<DeviceDescriptor> result;
    if(n<0) throw std::runtime_error(libusb_error_name(int(n)));
    for(ssize_t i=0;i<n;++i) {
        libusb_device_descriptor desc; if(libusb_get_device_descriptor(list[i],&desc)) continue;
        if(desc.idVendor==0x1f3a&&desc.idProduct==0xefe8) {
            auto d=Describe(Identity(list[i])); d.vid=desc.idVendor; d.pid=desc.idProduct; d.index=int(result.size()+1); d.mode="FEL/FES";
            if(!d.ports.empty()) result.push_back(d);
        }
    }
    libusb_free_device_list(list,1); return result;
}
std::string ModeName(uint16_t mode) { switch(mode) { case 1:return "FEL"; case 2:return "FES"; case 3:return "UPDATE_COOL"; case 4:return "UPDATE_HOT"; default:return "Unknown ("+std::to_string(mode)+")"; } }
std::string StorageName(uint32_t type) { const char *names[]={"NAND","SD","eMMC","SPI NOR","eMMC3","SPI NAND","SD1","eMMC0","UFS"}; return type<9?names[type]:"Unknown"; }
} }
