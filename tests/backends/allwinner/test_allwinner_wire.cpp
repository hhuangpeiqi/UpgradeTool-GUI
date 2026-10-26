#include "backends/allwinner/UsbWire.h"
#include "tests/support/Checks.h"
#include <vector>
using namespace core;
using namespace core::allwinner;
class Wire final : public UsbWire {
public:
    std::vector<int> sizes, endpoints;
    int limit = 65536, response = -2;
    bool stopAfterTransfer = false;
    void Open(const DeviceIdentity &, int &, int &) override {}
    void Close() override {}
    int Transfer(int endpoint, char *, int size) override {
        endpoints.push_back(endpoint); sizes.push_back(size);
        if (stopAfterTransfer) cancel->request_stop();
        return response == -2 ? std::min(size, limit) : response;
    }
};
int main() {
    int failures = 0;
    std::vector<char> data(131073);
    Wire wire;
    CHECK(wire.Bulk(0x01, data.data(), data.size()) == 0);
    CHECK((wire.sizes == std::vector<int>{65536, 65536, 1}));
    wire.sizes.clear(); wire.endpoints.clear(); wire.limit = 7;
    CHECK(wire.Bulk(0x81, data.data(), 20) == 0);
    CHECK((wire.sizes == std::vector<int>{20, 13, 6}));
    CHECK((wire.endpoints == std::vector<int>{0x81, 0x81, 0x81}));
    for (int invalid : {0, -1, 100}) {
        wire.response = invalid;
        CHECK(wire.Bulk(0x81, data.data(), 10) == EFEX_ERR_USB_TRANSFER);
    }
    CHECK(wire.Bulk(0x81, nullptr, 10) == EFEX_ERR_INVALID_PARAM);
    CHECK(wire.Bulk(0x81, data.data(), -1) == EFEX_ERR_INVALID_PARAM);
    CHECK(wire.Bulk(0x81, nullptr, 0) == 0);
    wire.response = -2; wire.limit = 65536; wire.sizes.clear();
    wire.cancel = std::make_shared<std::stop_source>(); wire.stopAfterTransfer = true;
    CHECK(wire.Bulk(0x01, data.data(), data.size()) == EFEX_ERR_USB_TRANSFER);
    CHECK(wire.sizes.size() == 1);
    wire.cleanup = true;
    CHECK(wire.Bulk(0x01, data.data(), 10) == 0);
    return failures ? 1 : 0;
}
