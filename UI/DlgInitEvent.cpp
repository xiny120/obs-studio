#include "DlgInitEvent.h"


const QEvent::Type DlgInitEvent::eventType = (QEvent::Type)QEvent::registerEventType();


DlgInitEvent::DlgInitEvent() :QEvent(eventType){
}


DlgInitEvent::~DlgInitEvent(){
}
