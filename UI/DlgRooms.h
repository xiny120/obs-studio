#pragma once

#include <QDialog>
#include "ui_DlgRooms.h"

class DlgRooms : public QDialog
{
	Q_OBJECT

public slots:
	void myClick();
protected:
	void showEvent(QShowEvent *event);
public:
	QJsonObject UrlRequestPost(const QString url, const QString data);
	DlgRooms(QWidget *parent = Q_NULLPTR);
	~DlgRooms();

private:
	Ui::DlgRooms ui;
};
