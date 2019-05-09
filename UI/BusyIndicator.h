#pragma once

#include <QDialog>
#include "ui_BusyIndicator.h"

class BusyIndicator : public QDialog
{
	Q_OBJECT

public slots:

public:
	BusyIndicator(QWidget *parent = Q_NULLPTR);
	~BusyIndicator();

private:
	Ui::BusyIndicator ui;
};
