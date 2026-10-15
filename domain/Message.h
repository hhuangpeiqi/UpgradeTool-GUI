#ifndef DOMAIN_MESSAGE_H
#define DOMAIN_MESSAGE_H
#include <string>
#include <vector>
namespace core {
// Stable source keys and arguments cross the core/UI boundary untranslated.
struct Message {
    std::string context,key;
    std::vector<std::string> arguments;
    std::string English() const {
        std::string text=key;
        for(size_t i=arguments.size();i>0;--i) {
            const auto token="%"+std::to_string(i);
            size_t at=0;
            while((at=text.find(token,at))!=std::string::npos) { text.replace(at,token.size(),arguments[i-1]); at+=arguments[i-1].size(); }
        }
        return text;
    }
};
}
#endif
