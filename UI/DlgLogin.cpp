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

bool cmp(const QJsonValue& o1 , const QJsonValue& o2 ) {
	int id1, id2;
	QJsonObject o11, o22;
	o11 = o1.toObject();
	o22 = o2.toObject();
	//qDebug() << o11 << o22;
	//o1.isBool();
	//o2.isBool();
	id1 = o11.value("ID").toInt(0);
	id2 = o22.value("ID").toInt(0);
	//qDebug() << id1 << id2;
	return id1 < id2;
}

DlgLogin::DlgLogin(QWidget *parent): QDialog(parent){
	ui.setupUi(this);
	//connect(ui.okButton, SIGNAL(clicked()), this, SLOT(ClickButton()));

	QJsonArray array1;
	QVector<QString>v_Name;
	QVector<int>v_Num;
	QJsonObject obj1;

	v_Name.append("fly");
	v_Name.append("zoom");
	v_Name.append("apple");
	v_Name.append("draw");

	v_Num.append(4);
	v_Num.append(1);
	v_Num.append(9);
	v_Num.append(7);

	for (int i = 0; i < 4; i++)
	{
		obj1.insert("Name", v_Name[i]);
		obj1.insert("Num", v_Num[i]);
		array1.append(obj1);
	}
	

	QJsonArray::iterator iter;
	qDebug() << array1;
	for (iter = array1.begin(); iter != array1.end(); iter++)
	{
		obj1 = (*iter).toObject();
		qDebug() << obj1;
	}






	QJsonArray array;
	QJsonObject obj;
	QString sb = "Name";
	QJsonValue val("wx0");
	obj.insert(sb,val);
	obj.insert("ID", QJsonValue(0));
	array.append(obj);
	obj.insert("Name", QJsonValue("wx2"));
	obj.insert("ID", QJsonValue(2));
	array.append(obj);
	obj.insert("Name", QJsonValue("wx1"));
	obj.insert("ID", QJsonValue(1));
	array.append(obj);
	qDebug() << array;
	std::sort(array.begin(), array.end(), cmp);
	qDebug() << array;
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

	QJsonObject  object = UrlRequestPost("http://www.gwgz.com:8091/api/1.00/public", strJson);
	if (object.contains("status")) {  // 包含指定的 key
		QJsonValue value = object.value("status");  // 获取指定 key 对应的 value
		if (value.isDouble()) {  // 判断 value 是否为字符串
			int status = value.toInt(1);
			if (status == 0 && (object.contains("userinfo"))) {
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
	QMessageBox box(QMessageBox::Warning, "", "用户名密码错误！");
	box.exec();

}

QJsonObject DlgLogin::UrlRequestPost(const QString url, const QString data){
	QJsonObject object;
	QNetworkAccessManager qnam;
	const QUrl aurl(url);
	QNetworkRequest qnr(aurl);
	qnr.setRawHeader("Content-Type", "application/json;charset=utf8");
	QNetworkReply *reply = qnam.post(qnr, data.toLocal8Bit());

	QEventLoop eventloop;
	connect(reply, SIGNAL(finished()), &eventloop, SLOT(quit()));
	eventloop.exec(QEventLoop::ExcludeUserInputEvents);

	//QTextCodec *codec = QTextCodec::codecForName("utf8");
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
