#pragma once

#include <QObject>

class TokenReader : public QObject
{
    Q_OBJECT
public:
    explicit TokenReader(std::string pathToTokenFile, QObject *parent = nullptr);
    std::string getToken();

private:
    std::string token;

    const std::string tokenValue = "discord_token";
};
