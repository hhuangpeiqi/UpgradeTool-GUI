#include "platform/Text.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string_view>
namespace platform {
namespace {
struct Mapping { uint32_t code; const char *replacement; };
#include "UnicodeData.inc"
uint32_t Decode(const std::string &s, size_t &i) {
    auto first = static_cast<unsigned char>(s.at(i++));
    if (first < 0x80) return first;
    unsigned count = first >= 0xf0 && first <= 0xf4 ? 3 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xc2 && first <= 0xdf ? 1 : 0;
    if (!count) throw std::runtime_error("Invalid UTF-8 text");
    uint32_t code = first & ((1u << (6 - count)) - 1);
    for (unsigned j=0; j<count; ++j) {
        if (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) throw std::runtime_error("Invalid UTF-8 text");
        code = (code << 6) | (static_cast<unsigned char>(s[i++]) & 0x3f);
    }
    if (code < (count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) throw std::runtime_error("Invalid UTF-8 text");
    return code;
}
template<size_t N> std::string Map(const std::string &text, const Mapping (&map)[N]) {
    std::string out;
    for (size_t i=0; i<text.size();) {
        size_t start=i; auto code=Decode(text,i);
        auto found=std::lower_bound(std::begin(map),std::end(map),code,[](const Mapping &a,uint32_t b) { return a.code<b; });
        if (found!=std::end(map) && found->code==code) out += found->replacement;
        else out.append(text,start,i-start);
    }
    return out;
}
}
std::string Trim(const std::string &text) {
    const auto begin=text.find_first_not_of(" \t\n\r\v\f");
    if (begin==std::string::npos) return {};
    return text.substr(begin,text.find_last_not_of(" \t\n\r\v\f")-begin+1);
}
std::string Upper(const std::string &text) { return Map(text, upper); }
std::string FoldCase(const std::string &text) { return Map(text, folded); }
std::string PrefixCharacters(const std::string &text, size_t count) {
    size_t i=0; while (count-- && i<text.size()) Decode(text,i);
    return text.substr(0,i);
}
uint64_t Unsigned(const std::string &input, int base) {
    std::string text=Trim(input);
    if (!text.empty() && text.front()=='+') text.erase(0,1);
    if (base==16 && text.starts_with("0x")) text.erase(0,2);
    uint64_t result=0;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),result,base);
    if (text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) throw std::runtime_error("Invalid unsigned number");
    return result;
}
}
