#include "backends/allwinner/UsbWire.h"
#include <stdexcept>
namespace {
int Bulk(void *opaque, int ep, char *data, ssize_t length) {
    auto *wire = static_cast<core::allwinner::UsbWire *>(opaque);
    return wire ? wire->Bulk(ep, data, length) : EFEX_ERR_INVALID_PARAM;
}
}
extern "C" int sunxi_usb_bulk_send(void *h,int ep,const char *b,ssize_t n) { return Bulk(h,ep,const_cast<char *>(b),n); }
extern "C" int sunxi_usb_bulk_recv(void *h,int ep,char *b,ssize_t n) { return Bulk(h,ep,b,n); }
namespace core { namespace allwinner {
namespace {
class Transport : public ITransport {
    sunxi_efex_ctx_t ctx = {};
    std::unique_ptr<UsbWire> wire;
    void Check(int rc) { if(rc) throw std::runtime_error(std::string(sunxi_efex_strerror(rc)) + (wire->error.empty() ? "" : ": " + wire->error)); }
public:
    explicit Transport(std::shared_ptr<platform::UsbContext> context) : wire(CreateUsbWire(std::move(context))) {}
    ~Transport() override { Close(); }
    void Close() override { wire->Close(); ctx = {}; }
    void Open(const DeviceIdentity &target,const Cancellation &cancel) override {
        Close(); wire->cancel = cancel; wire->error.clear(); wire->cleanup = false;
        try {
            int in = 0, out = 0;
            wire->Open(target, in, out);
            ctx.epin = in; ctx.epout = out; ctx.hdl = wire.get();
            Check(sunxi_efex_init(&ctx));
        } catch (...) { Close(); throw; }
    }
    DeviceDetails Details() const override { DeviceDetails d; d.chip=ctx.resp.id; d.firmware=ctx.resp.firmware; d.mode=ctx.resp.mode; return d; }
    void FelWrite(uint32_t addr,const Bytes &b) override { Check(sunxi_efex_fel_write(&ctx,addr,reinterpret_cast<const char *>(b.data()),b.size())); }
    Bytes FelRead(uint32_t addr,size_t n) override { Bytes b(n); Check(sunxi_efex_fel_read(&ctx,addr,reinterpret_cast<char *>(b.data()),b.size())); return b; }
    void FelExec(uint32_t addr) override { Check(sunxi_efex_fel_exec(&ctx,addr)); }
    uint32_t QueryStorage() override { uint32_t v=0; Check(sunxi_efex_fes_query_storage(&ctx,&v)); return U32(reinterpret_cast<uint8_t *>(&v)); }
    uint32_t QuerySecure() override { uint32_t v=0; Check(sunxi_efex_fes_query_secure(&ctx,&v)); return U32(reinterpret_cast<uint8_t *>(&v)); }
    uint32_t QuerySize() override { uint32_t v=0; Check(sunxi_efex_fes_probe_flash_size(&ctx,&v)); return U32(reinterpret_cast<uint8_t *>(&v)); }
    void FlashAccess(bool on) override {
        uint32_t type=0; wire->cleanup=!on;
        int rc=sunxi_efex_fes_flash_set_onoff(&ctx,&type,on?1:0); wire->cleanup=false; Check(rc);
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
std::string ModeName(uint16_t mode) { switch(mode) { case 1:return "FEL"; case 2:return "FES"; case 3:return "UPDATE_COOL"; case 4:return "UPDATE_HOT"; default:return "Unknown ("+std::to_string(mode)+")"; } }
std::string StorageName(uint32_t type) { const char *names[]={"NAND","SD","eMMC","SPI NOR","eMMC3","SPI NAND","SD1","eMMC0","UFS"}; return type<9?names[type]:"Unknown"; }
} }
