// Derived from Rockchip RKLog (GPL-2.0+); see backends/rockchip/legacy/license.txt.
#include "platform/FileLog.h"
#include "platform/File.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <vector>
namespace platform {
FileLog::FileLog(std::string directory,std::string name,bool enabled)
    : m_name(name.empty()?"Log":std::move(name)),m_enabled(enabled) {
    std::error_code error;
    if(std::filesystem::is_directory(Path(directory),error)) m_path=std::move(directory);
}
void FileLog::Record(const char *format,...) {
    if(!m_enabled.load() || m_path.empty()) return;
    va_list args, countArgs; va_start(args,format); va_copy(countArgs,args);
    const int length=std::vsnprintf(nullptr,0,format,countArgs); va_end(countArgs);
    if(length<0) { va_end(args); return; }
    std::vector<char> text(size_t(length)+1);
    std::vsnprintf(text.data(),text.size(),format,args); va_end(args);
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now=std::time(nullptr); std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&now);
#else
    localtime_r(&now,&local);
#endif
    char date[32],stamp[32]; std::strftime(date,sizeof(date),"%Y-%m-%d.txt",&local);
    std::strftime(stamp,sizeof(stamp),"%H:%M:%S \t",&local);
    std::ofstream out(Path(m_path)/Path(m_name+date),std::ios::binary|std::ios::app);
    if(out) { out<<stamp; out.write(text.data(),length); out<<"\r\n"; }
}
bool FileLog::SaveBuffer(std::string fileName,unsigned char *bytes,uint32_t size) {
    try { AtomicFile out(Path(fileName)); out.write(bytes,size); out.commit(); return true; }
    catch(const std::exception &) { return false; }
}
void FileLog::PrintBuffer(std::string &out,unsigned char *bytes,uint32_t size,unsigned lineCount) {
    out.clear(); unsigned count=0; char hex[16];
    for(uint32_t i=0;i<size;++i,++count) {
        std::snprintf(hex,sizeof(hex),"%X",bytes[i]); out+=' '; out+=hex;
        if(count>=lineCount) { out+="\r\n"; count=0; }
    }
}
}
