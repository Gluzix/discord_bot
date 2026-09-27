#include "PlayingPanel.h"

#include <dpp/dpp.h>
#include <QDebug>
#include <QString>

PlayingPanel::PlayingPanel(dpp::cluster &bot_, uint64_t channelId_, std::string title_)
    : bot(bot_)
    , channelId(channelId_)
    , title(std::move(title_))
{
}

void PlayingPanel::announced(const dpp::confirmation_callback_t &answer)
{
    if (answer.is_error()) {
        qWarning().noquote() << "The \"Playing:\" message was not sent:"
                             << QString::fromStdString(answer.get_error().message);
        return;
    }
    messageId = static_cast<uint64_t>(answer.get<dpp::message>().id);
}

void PlayingPanel::retire(const std::shared_ptr<PlayingPanel> &panel)
{
    if (!panel) {
        return;
    }

    // The cluster runs this callback, so it outlives the call.
    dpp::cluster *cluster = &panel->bot;
    cluster->start_timer([panel, cluster](dpp::timer handle) {
        cluster->stop_timer(handle); // one-shot
        const uint64_t id = panel->messageId;
        if (id == 0) {
            qDebug() << "A \"Playing:\" message keeps its buttons: its id has not come back yet";
            return;
        }

        dpp::message bare(panel->channelId, panel->title);
        bare.id = id;
        // The title is untrusted input, and it goes out again.
        bare.set_allowed_mentions();
        cluster->message_edit(bare, [](const dpp::confirmation_callback_t &answer) {
            if (answer.is_error()) {
                qWarning().noquote() << "Taking the buttons off a \"Playing:\" message failed:"
                                     << QString::fromStdString(answer.get_error().message);
            }
        });
    }, RETIRE_AFTER_SECONDS);
}
