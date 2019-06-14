#include "DlgRooms.h"
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
#include <thread>
#include "obs-app.hpp"
#include "DlgInitEvent.h"
#include "BusyIndicator.h"


DlgRooms::DlgRooms(QWidget *parent): QDialog(parent){
	ui.setupUi(this);
	connect(ui.listRooms, &QListWidget::currentRowChanged, [=](int curindex) {
		qDebug() << curindex << endl;
	});
	//connect(ui.okButton, SIGNAL(clicked()), this, SLOT(myClick()));
	
}

DlgRooms::~DlgRooms(){
}

void DlgRooms::showEvent(QShowEvent *event) {
	// https://blog.csdn.net/y396397735/article/details/82314458
	qApp->postEvent(this, new DlgInitEvent());

}

bool DlgRooms::event(QEvent *e){
	if (e->type() == DlgInitEvent::eventType) {
		BusyIndicator bi(this);// = new BusyIndicator(this);
		bi.show();
		bool runResult{ false };
		QEventLoop loop;
		
		connect(this, &DlgRooms::signalRunOver, &loop, &QEventLoop::quit);
		std::thread testThread([&]{

			QJsonObject json;
			json.insert("action", "pushroomlist");
			json.insert("mster-token", App()->ui.SessionId);
			QJsonDocument document;
			document.setObject(json);
			QByteArray byteArray = document.toJson(QJsonDocument::Compact);
			QString strJson(byteArray);

			QJsonObject  object = UrlRequestPost("http://www.gwgz.com:8091/api/1.00/private", strJson);
			qDebug() << object;
			if (object.contains("status")) {  // 包含指定的 key
				QJsonValue value = object.value("status");  // 获取指定 key 对应的 value
				if (value.isDouble()) {  // 判断 value 是否为字符串
					int status = value.toInt(1);
					if (status == 0 && (object.contains("roomlist"))) {
						QJsonArray rooms = object.value("roomlist").toArray();
						QJsonArray::iterator iter;
						for (iter = rooms.begin(); iter != rooms.end(); iter++) {
							object = (*iter).toObject();
							qDebug() << object;
							int Id = _int64(object.value("roomid").toDouble());
							QString Title = object.value("title").toString();
							QString Icon = object.value("icon").toString();
							QString PushUri = "rtmp://" + object.value("vhost").toString()
								+ ":" + QString::number(int(object.value("vport").toDouble()))
								+ "/" + object.value("vapp").toString()
								+ "";// +object.value("vstream").toString();
							qDebug() << PushUri;
							QString Key = object.value("vstream").toString();
							QListWidgetItem* qwi = new QListWidgetItem();
							qwi->setText(Title);
							qwi->setData(Qt::UserRole, Id);
							qwi->setData(Qt::UserRole + 1, PushUri);
							qwi->setData(Qt::UserRole + 2, Key);
							ui.listRooms->addItem(qwi);

						}
						
					}
				}
			}
			runResult = true;
			emit signalRunOver();
		});
		testThread.detach();
		loop.exec();
		if (!runResult){
		}
	}
	return QDialog::event(e);
}

void DlgRooms::myClick() {
	QListWidgetItem * pItem = ui.listRooms->currentItem();
	if (pItem == nullptr) {
		QMessageBox box(QMessageBox::Warning, "", "请选择一个房间进行直播！");
		box.exec();
		return;
	}
	QString roomid = QString::number(pItem->data(Qt::UserRole).toInt());
	QString PushUri = pItem->data(Qt::UserRole + 1).toString();
	QString key = pItem->data(Qt::UserRole + 2).toString();
	App()->ui.RoomId = roomid;
	App()->ui.PushUri = PushUri;
	App()->ui.Key = key;
	this->accept();
}

QJsonObject DlgRooms::UrlRequestPost(const QString url, const QString data){
	QJsonObject object;
	QNetworkAccessManager qnam;
	const QUrl aurl(url);
	QNetworkRequest qnr(aurl);
	qnr.setRawHeader("Content-Type", "application/json;charset=utf8");
	qnr.setRawHeader("mster-token", App()->ui.SessionId.toUtf8());
	QNetworkReply *reply = qnam.post(qnr, data.toLocal8Bit());
	QEventLoop eventloop;
	connect(reply, SIGNAL(finished()), &eventloop, SLOT(quit()));
	eventloop.exec(QEventLoop::ExcludeUserInputEvents);
	QTextCodec *codec = QTextCodec::codecForName("utf8");
	QByteArray  buf = reply->readAll();
	QJsonParseError jsonError;
	QJsonDocument doucment = QJsonDocument::fromJson(buf, &jsonError);  // 转化为 JSON 文档
	if (!doucment.isNull() && (jsonError.error == QJsonParseError::NoError)) {  // 解析未发生错误
		if (doucment.isObject()) { // JSON 文档为对象
			object = doucment.object();  // 转化为对象
		}
	}
	reply->deleteLater();
	reply = 0;
	return object;
}
