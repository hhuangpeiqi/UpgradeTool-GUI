#ifndef DOMAIN_DEVICE_H
#define DOMAIN_DEVICE_H
#include "domain/Vendor.h"
#include <memory>
#include <string>
namespace core {
class DeviceToken {
public:
    virtual ~DeviceToken() = default;
    virtual bool Matches(const DeviceToken &) const = 0;
};
class DeviceAccess;
// Immutable enumeration snapshot. Presentation data cannot manufacture a target.
class DeviceRef {
public:
    DeviceRef() = default;
    Vendor vendor() const { return m_vendor; }
    const std::string &key() const { return m_key; }
    const std::string &serial() const { return m_serial; }
    explicit operator bool() const { return bool(m_token); }
    bool Matches(const DeviceRef &candidate) const {
        return m_vendor==candidate.m_vendor && m_token && candidate.m_token && m_token->Matches(*candidate.m_token);
    }
    friend bool operator==(const DeviceRef &a,const DeviceRef &b) {
        if(!a && !b) return true;
        return a.m_key==b.m_key && a.Matches(b) && b.Matches(a);
    }
private:
    friend class DeviceAccess;
    Vendor m_vendor=Vendor::Rockchip;
    std::string m_key,m_serial;
    std::shared_ptr<const DeviceToken> m_token;
};
}
#endif
