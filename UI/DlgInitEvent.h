#pragma once
#include <QEvent>

class DlgInitEvent :public QEvent
{
public:
	DlgInitEvent();
	virtual ~DlgInitEvent();

	static const Type eventType;
};

