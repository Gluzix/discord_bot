#include "TokenReader.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

TokenReader::TokenReader(std::string pathToTokenFile, QObject *parent)
    : QObject{parent}
{
    QByteArray reader;
    QFile file(QString::fromStdString(pathToTokenFile));

    if (file.open(QFile::OpenModeFlag::ReadOnly))
    {
        reader = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(reader);
        QJsonObject json = doc.object();
        token = json.value(QString::fromStdString(tokenValue)).toString().toStdString();
    }
}

std::string TokenReader::getToken()
{
    return token;
}
