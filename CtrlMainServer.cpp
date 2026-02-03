#include "CtrlMainServer.h"
#include "TokenReader.h"

#include <dpp/dpp.h>

CtrlMainServer::CtrlMainServer()
{
    TokenReader reader("token.json");
    std::string token = reader.getToken();
    bot = std::make_shared<dpp::cluster>(token);
    bot->on_log(dpp::utility::cout_logger());

    cmdHandler.setBot(bot);
    cmdHandler.prepare();
}

void CtrlMainServer::run()
{
    bot->start(dpp::st_wait);
}
