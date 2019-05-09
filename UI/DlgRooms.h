#pragma once

#include <QDialog>
#include "ui_DlgRooms.h"

class DlgRooms : public QDialog
{
	Q_OBJECT

signals:
	// 线程执行结束后发送此信号
	void signalRunOver();

public slots:
	void myClick();
protected:
	void showEvent(QShowEvent *event);
	bool event(QEvent *event);
	
public:
	QJsonObject UrlRequestPost(const QString url, const QString data);
	DlgRooms(QWidget *parent = Q_NULLPTR);
	~DlgRooms();

private:
	Ui::DlgRooms ui;
};
