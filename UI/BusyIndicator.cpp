#include "BusyIndicator.h"
#include <QCryptographicHash>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QEventLoop>
#include <QTextCodec>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <qmovie.h>
#include "obs-app.hpp"


BusyIndicator::BusyIndicator(QWidget *parent): QDialog(parent){
	ui.setupUi(this);
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
	QSize qs0(112,84);
	QMovie *movie = new QMovie(":/res/images/loading.gif");
	ui.label->setGeometry(0, 0, 112, 84);
	movie->setScaledSize(qs0);
	ui.label->setMovie(movie);
	movie->start();
}

BusyIndicator::~BusyIndicator(){
}
