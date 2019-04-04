#include "DlgLogin.h"
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
#include "obs-app.hpp"


DlgLogin::DlgLogin(QWidget *parent): QDialog(parent){
	ui.setupUi(this);
	//connect(ui.okButton, SIGNAL(clicked()), this, SLOT(ClickButton()));
}

DlgLogin::~DlgLogin(){
}

void DlgLogin::myClick() {
	QString un = ui.ltUserName->text();
	QString pwd = ui.lePassword->text();
	QString md5;
	QByteArray ba, bb;
	QCryptographicHash md(QCryptographicHash::Md5);
	ba.append(pwd);
	md.addData(ba);
	bb = md.result();
	md5.append(bb.toHex());
	md5 = md5.mid(8, 16);

	QJsonObject json;
	json.insert("action", "auth");
	json.insert("account", un);
	json.insert("password", md5);
	QJsonDocument document;
	document.setObject(json);
	QByteArray byteArray = document.toJson(QJsonDocument::Compact);
	QString strJson(byteArray);

	QJsonObject  object = UrlRequestPost("http://www.gwgz.com:8091//api/1.00/public", strJson);
	if (object.contains("status")) {  // 包含指定的 key
		QJsonValue value = object.value("status");  // 获取指定 key 对应的 value
		if (value.isDouble()) {  // 判断 value 是否为字符串
			int status = value.toInt(1);
			if (status == 0) {
				if (object.contains("userinfo")) {
					value = object.value("userinfo");
					if (value.isObject()) {
						object = value.toObject();
						if (object.contains("UserId")) {
							int UserId = object.value("UserId").toInt(0);
							if (UserId != 0) {
								this->accept();
								App()->ui.UserName = object.value("UserName").toString();
								App()->ui.SessionId = object.value("SessionId").toString();
								App()->ui.Token = object.value("Token").toString();
								return;
							}
							else {
							}
						}
					}
				}
			}

		}
	}
	QMessageBox box(QMessageBox::Warning, "", "用户名密码错误！");
	box.exec();

}

QJsonObject DlgLogin::UrlRequestPost(const QString url, const QString data)
{
	QJsonObject object;
	QNetworkAccessManager qnam;
	const QUrl aurl(url);
	QNetworkRequest qnr(aurl);
	qnr.setRawHeader("Content-Type", "application/json;charset=utf8");
	QNetworkReply *reply = qnam.post(qnr, data.toLocal8Bit());

	QEventLoop eventloop;
	connect(reply, SIGNAL(finished()), &eventloop, SLOT(quit()));
	eventloop.exec(QEventLoop::ExcludeUserInputEvents);

	QTextCodec *codec = QTextCodec::codecForName("utf8");
	QByteArray  buf = reply->readAll();
	//QString replyData = codec->toUnicode(reply->readAll());
	//if (replyData.length > 0) {

		QJsonParseError jsonError;
		QJsonDocument doucment = QJsonDocument::fromJson(buf, &jsonError);  // 转化为 JSON 文档
		if (!doucment.isNull() && (jsonError.error == QJsonParseError::NoError)) {  // 解析未发生错误
			if (doucment.isObject()) { // JSON 文档为对象
				object = doucment.object();  // 转化为对象
			}
		}
	//}

	
	reply->deleteLater();
	reply = 0;

	return object;
}
