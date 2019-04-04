#pragma once

#include <QDialog>
#include "ui_DlgLogin.h"

class DlgLogin : public QDialog
{
	Q_OBJECT

public slots:
	void myClick();

public:
	QJsonObject UrlRequestPost(const QString url, const QString data);
	DlgLogin(QWidget *parent = Q_NULLPTR);
	~DlgLogin();

private:
	Ui::DlgLogin ui;
};
