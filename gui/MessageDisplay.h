#ifndef GUI_MESSAGE_DISPLAY_H
#define GUI_MESSAGE_DISPLAY_H
#include "domain/Message.h"
#include <QCoreApplication>
namespace gui {
inline QString Display(const core::Message &message) {
    QString text=QCoreApplication::translate(message.context.c_str(),message.key.c_str());
    for(size_t i=message.arguments.size();i>0;--i) text.replace("%"+QString::number(i),QString::fromStdString(message.arguments[i-1]));
    return text;
}
}
#endif
