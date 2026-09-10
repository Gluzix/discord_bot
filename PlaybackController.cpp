#include "PlaybackController.h"
#include "WindowsProcessRunner.h"

#include <dpp/dpp.h>
#include <QDebug>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

PlaybackController::PlaybackController()
{
    workerThread = std::thread(&PlaybackController::playbackWorker, this);
}

PlaybackController::~PlaybackController()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        running = false;
        songQueue.clear();
    }
    stopSendingData();
    stateCv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

size_t PlaybackController::play(const std::string &youtubeUrl, const dpp::slashcommand_t &event)
{
    size_t waitingPosition = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);

        bool busy = songActive || !songQueue.empty();

        Song song;
        song.youtubeUrl = youtubeUrl;
        song.event = std::make_unique<dpp::slashcommand_t>(event);
        songQueue.push_back(std::move(song));

        // A ready voice connection travels with the request; if the
        // handshake is still in flight, songs simply wait in the queue
        // until onVoiceReady provides the client.
        dpp::voiceconn* vc = event.from()->get_voice(event.command.guild_id);
        if (vc && vc->voiceclient && vc->voiceclient->is_ready()) {
            currentVoiceClient = vc->voiceclient.get();
        }

        if (busy) {
            waitingPosition = songQueue.size();
        }
    }
    stateCv.notify_all();
    return waitingPosition;
}

bool PlaybackController::skip()
{
    dpp::discord_voice_client *voiceClient = nullptr;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!songActive) {
            return false;
        }
        voiceClient = currentVoiceClient;
    }

    // Ending the current song is enough - the worker joins its threads and
    // advances to the next queued one on its own.
    stopSendingData();
    if (voiceClient) {
        voiceClient->stop_audio();
    }
    return true;
}

void PlaybackController::stop()
{
    dpp::discord_voice_client *voiceClient = nullptr;
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        songQueue.clear();
        wasActive = songActive;
        voiceClient = currentVoiceClient;
        // The pointer dies with the voice connection on /leave; drop it so
        // the worker can't start a queued song on a dead client. The next
        // /play or onVoiceReady provides a fresh one.
        currentVoiceClient = nullptr;
    }

    stopSendingData();
    if (wasActive && voiceClient) {
        voiceClient->stop_audio();
    }
}

void PlaybackController::onVoiceReady(const dpp::voice_ready_t &event)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentVoiceClient = event.voice_client;
    }
    stateCv.notify_all();
}

PlaybackController::QueueSnapshot PlaybackController::queueSnapshot()
{
    QueueSnapshot snapshot;
    std::lock_guard<std::mutex> lock(stateMutex);
    snapshot.current = currentSongLabel;
    for (const Song &song : songQueue) {
        snapshot.queued.push_back(song.youtubeUrl);
    }
    return snapshot;
}

void PlaybackController::playbackWorker()
{
    while (running) {
        Song song;
        dpp::discord_voice_client *voiceClient = nullptr;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            stateCv.wait(lock, [this] {
                return !running || (!songQueue.empty() && currentVoiceClient != nullptr);
            });
            if (!running) {
                break;
            }
            song = std::move(songQueue.front());
            songQueue.pop_front();
            voiceClient = currentVoiceClient;
            songActive = true;
            currentSongLabel = song.youtubeUrl;
        }

        // Blocks until the song ends naturally or is skipped/stopped;
        // finishing this call IS the auto-advance to the next loop turn.
        playSong(voiceClient, std::move(song));

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            songActive = false;
            currentSongLabel.clear();
        }
    }
}

void PlaybackController::playSong(dpp::discord_voice_client *voiceClient, Song song)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = false;
        audioQueue = {};
    }
    requestedUrl = song.youtubeUrl;
    isPlaying = true;

    resamplingThread = std::thread(&PlaybackController::pcmResample, this, *song.event);
    audioThread = std::thread(&PlaybackController::streamAudio, this, voiceClient);

    resamplingThread.join();
    audioThread.join();
}

void PlaybackController::stopSendingData()
{
    isPlaying = false;
    queueCv.notify_all();
}

void PlaybackController::streamAudio(dpp::discord_voice_client *voiceClient)
{
    // Everything handed to DPP is opus-encoded and (with DAVE E2EE, the
    // default) encrypted with the *current* group key immediately. The key
    // rotates whenever someone joins or leaves, turning any large queued
    // backlog into silence for the listeners - so keep DPP's queue short
    // and hold the deep buffer here as PCM, which no rekey can spoil.
    const float MAX_BUFFERED_SECONDS = 1.0f;

    // The decoder fills the queue with ready-to-send packets of exactly
    // dpp::send_audio_raw_max_length bytes; only the final one may be shorter.
    while (isPlaying) {
        std::vector<uint8_t> packet;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] {
                return !audioQueue.empty() || decodingFinished || !isPlaying;
            });
            if (audioQueue.empty()) {
                if (decodingFinished) break;
                continue;
            }
            packet = std::move(audioQueue.front());
            audioQueue.pop();
        }

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            while (isPlaying && voiceClient->get_secs_remaining() > MAX_BUFFERED_SECONDS) {
                queueCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return !isPlaying;
                });
            }
        }
        if (!isPlaying) break;

        voiceClient->send_audio_raw((uint16_t*)packet.data(), packet.size());
    }

    isPlaying = false;
}

void PlaybackController::pcmResample(dpp::slashcommand_t event)
{
    // Whatever happens here, the sender thread waits on the queue and must
    // be released - every exit path has to mark decoding as finished.
    auto signalFinished = [this]() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            decodingFinished = true;
        }
        queueCv.notify_one();
    };

    ResolvedMedia media = WindowsProcessRunner::resolveMedia(requestedUrl);
    if (media.directUrl.empty()) {
        event.edit_original_response(dpp::message("Couldn't get the audio from that link :("));
        signalFinished();
        return;
    }

    if (!media.title.empty()) {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentSongLabel = media.title;
    }

    std::string directUrl = media.directUrl;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "headers",
                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                0);
    av_dict_set(&options, "buffer_size", "1048576", 0); // 1MB read buffer
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "multiple_requests", "1", 0);
    AVFormatContext *format = nullptr;
    int errorCode = avformat_open_input(&format, directUrl.c_str(), nullptr, &options);
    av_dict_free(&options); // open_input consumed what it needed

    if (errorCode != 0) {
        char errbuf[256];
        av_strerror(errorCode, errbuf, sizeof(errbuf));
        qDebug() << "Cannot open input! avformat_open_input returned with " << errorCode << errbuf;
        event.edit_original_response(dpp::message("Couldn't open the audio stream :("));
        signalFinished();
        return;
    }

    errorCode = avformat_find_stream_info(format, nullptr);
    if (errorCode != 0) {
        qDebug() << "Cannot find stream info! avformat_find_stream_info returned with " << errorCode;
        avformat_close_input(&format);
        event.edit_original_response(dpp::message("Couldn't read the audio stream :("));
        signalFinished();
        return;
    }

    int audioStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStream < 0) {
        qDebug() << "Couldn't find audio stream! av_find_best_stream returned with " << audioStream;
        avformat_close_input(&format);
        event.edit_original_response(dpp::message("That link has no audio stream :("));
        signalFinished();
        return;
    }

    // The title is untrusted input from the video page - disable every kind
    // of mention so a title like "@everyone" can't ping the server.
    dpp::message nowPlaying(media.title.empty() ? "Playing!" : "Playing: **" + media.title + "**");
    nowPlaying.set_allowed_mentions();
    event.edit_original_response(nowPlaying);

    const AVCodec *codec = avcodec_find_decoder(format->streams[audioStream]->codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codec_ctx, format->streams[audioStream]->codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    SwrContext* swr = nullptr;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_ch_layout = codec_ctx->ch_layout;

    swr_alloc_set_opts2(&swr, &out_ch_layout,
                        AV_SAMPLE_FMT_S16,
                        48000,
                        &in_ch_layout,
                        codec_ctx->sample_fmt,
                        codec_ctx->sample_rate,
                        0, nullptr);

    swr_init(swr);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // The queue carries ready-to-send packets of exactly PACKET_SIZE bytes.
    // DPP drops the remainder of larger sends and silence-pads smaller ones,
    // so the invariant is enforced here, at the single point of production.
    const size_t PACKET_SIZE = dpp::send_audio_raw_max_length;
    const size_t BYTES_PER_SAMPLE_PAIR = 4; // s16 stereo
    std::vector<uint8_t> staging;

    auto convertIntoStaging = [&](const uint8_t **inData, int inSamples) {
        int maxOutSamples = swr_get_out_samples(swr, inSamples);
        if (maxOutSamples <= 0) return;
        size_t writePos = staging.size();
        staging.resize(writePos + (size_t)maxOutSamples * BYTES_PER_SAMPLE_PAIR);
        uint8_t *outPlane = staging.data() + writePos;
        int converted = swr_convert(swr, &outPlane, maxOutSamples, inData, inSamples);
        staging.resize(writePos + (converted > 0 ? (size_t)converted * BYTES_PER_SAMPLE_PAIR : 0));
    };

    auto pushFullPackets = [&]() {
        size_t readPos = 0;
        while (staging.size() - readPos >= PACKET_SIZE) {
            std::vector<uint8_t> pkt(staging.begin() + readPos,
                                     staging.begin() + readPos + PACKET_SIZE);
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                audioQueue.push(std::move(pkt));
            }
            queueCv.notify_one();
            readPos += PACKET_SIZE;
        }
        staging.erase(staging.begin(), staging.begin() + readPos);
    };

    auto readTime = std::chrono::steady_clock::duration::zero();
    auto decodeTime = std::chrono::steady_clock::duration::zero();

    while (isPlaying) {
        auto t0 = std::chrono::steady_clock::now();
        int ret = av_read_frame(format, packet);
        auto t1 = std::chrono::steady_clock::now();
        readTime += (t1 - t0);

        if (ret < 0) break;

        if (packet->stream_index != audioStream) {
            av_packet_unref(packet);
            continue;
        }

        auto t2 = std::chrono::steady_clock::now();
        if (avcodec_send_packet(codec_ctx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }

        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
            convertIntoStaging((const uint8_t**)frame->data, frame->nb_samples);
            pushFullPackets();
            av_frame_unref(frame);
        }
        auto t3 = std::chrono::steady_clock::now();
        decodeTime += (t3 - t2);

        av_packet_unref(packet);
    }

    qDebug() << "Total read (network) time: " << std::chrono::duration_cast<std::chrono::milliseconds>(readTime).count() << "ms";
    qDebug() << "Total decode/resample time: " << std::chrono::duration_cast<std::chrono::milliseconds>(decodeTime).count() << "ms";

    // Drain the resampler; only this last packet may be shorter than
    // PACKET_SIZE - DPP silence-pads it, inaudible at end of stream.
    convertIntoStaging(nullptr, 0);
    pushFullPackets();
    if (!staging.empty()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            audioQueue.push(std::move(staging));
        }
        queueCv.notify_one();
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format);

    signalFinished();
}
