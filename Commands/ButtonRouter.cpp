#include "ButtonRouter.h"
#include "ICommand.h"
#include "PlaybackController.h"
#include "PlaybackButtons.h"
#include "Interactions.h"
#include "Messages.h"

#include <dpp/dpp.h>
#include <QDebug>
#include <QString>

#include <chrono>

ButtonRouter::ButtonRouter(const std::unordered_map<std::string, std::unique_ptr<ICommand>> &commands_,
                           std::shared_ptr<PlaybackController> playback_)
    : commands(commands_)
    , playback(playback_)
{
}

void ButtonRouter::handle(const dpp::button_click_t &event)
{
    const QString name = QString::fromStdString("button " + event.custom_id);
    qDebug().noquote() << name << "received" << interactions::ageMs(event)
                       << "ms after it was issued";
    const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();

    std::optional<buttons::Click> click = buttons::parse(event.custom_id);
    if (click && click->command == buttons::PLAY_PAUSE) {
        click->command = playback->isPaused() ? "resume" : "pause";
    }
    auto command = click ? commands.find(click->command) : commands.end();
    if (command == commands.end()) {
        qDebug().noquote() << name << "is not a known button";
        interactions::refuse(event, messages::unknownButton);
        return;
    }

    command->second->execute(event, click->argument);
    qDebug().noquote() << name << "handled in"
                       << std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - startedAt).count() << "ms";
}
