#include <dpp/dpp.h>
#include "TokenReader.h"

int main(int argc, char *argv[])
{
    TokenReader reader("token.json");
    std::string token = reader.getToken();

    dpp::cluster bot(token);
    bot.on_log(dpp::utility::cout_logger());

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            event.reply("Kamiloes to cwel");
        }
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            bot.global_command_create(dpp::slashcommand("ping", "Kamiloes to cwel", bot.me.id));
        }
    });

    bot.start(dpp::st_wait);

    return 0;
}
