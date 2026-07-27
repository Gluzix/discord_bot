#include "Mp3Encoder.h"
#include <QDebug>
#include <iostream>

extern "C" {
    #include <libavutil/frame.h>
    #include <libavutil/mem.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
}

Mp3Encoder::Mp3Encoder() {}

void Mp3Encoder::openFile()
{
}

// void Mp3Encoder::PcmResample()
// {
//     pcmData.clear();

//     AVDictionary *options = nullptr;
//     av_dict_set(&options, "headers",
//                 "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
//                 0);
//     AVFormatContext *format = nullptr;
//     int errorCode = avformat_open_input(&format, "https://rr1---sn-cxn3pqhxqp5-3g3e.googlevideo.com/videoplayback?expire=1785185296&ei=sG9napKgCZmSv_IPzLa1mQk&ip=109.95.112.195&id=o-AJHRmv05ILM_w4WbAn904DV0q5Wzqp5H3zkRS6MAyLCm&itag=251&source=youtube&requiressl=yes&xpc=EgVo2aDSNQ%3D%3D&cps=268&met=1785163696%2C&mh=54&mm=31%2C29&mn=sn-cxn3pqhxqp5-3g3e%2Csn-f5f7kn7e&ms=au%2Crdu&mv=m&mvi=1&pl=21&rms=au%2Cau&initcwndbps=3271250&bui=AZFlqhNN72Vegubwt80XabSJxZnY5q3Az3R2xiUxkTUmhNRMuCyvYkBvvFWaIOjcx5BBMpOT5YfbBm4s&spc=SQ-umuEfHRJPdI_ESAJwdEkRqlbFVViim3oxbDH2VU7Z&vprv=1&svpuc=1&mime=audio%2Fwebm&rqh=1&gir=yes&clen=3205142&dur=211.161&lmt=1727674337092912&mt=1785163273&fvip=3&keepalive=yes&fexp=51565116%2C51992867&c=ANDROID_VR&txp=8208224&sparams=expire%2Cei%2Cip%2Cid%2Citag%2Csource%2Crequiressl%2Cxpc%2Cbui%2Cspc%2Cvprv%2Csvpuc%2Cmime%2Crqh%2Cgir%2Cclen%2Cdur%2Clmt&sig=AE0s2JYwRAIgdpyjzic-8lBiDVcY4KhRb8Iw7bvEf46jJWRDaxlX0MUCIAZmqPrxnXCfPAv2gUCVPMV8oqe4FJ9BzUl4NODW4LH8&lsparams=cps%2Cmet%2Cmh%2Cmm%2Cmn%2Cms%2Cmv%2Cmvi%2Cpl%2Crms%2Cinitcwndbps&lsig=APaTxxMwRAIgf1b79qlv_wPj_FhRxYPrAMOJknrhGoJGo7hkAWedA_MCIAdGdavFfA3l73I9zCkGOWXFoAaeeB_A_e5L2flRFmaK", nullptr, &options);

//     if (errorCode < 0) {
//         char errbuf[256];
//         av_strerror(errorCode, errbuf, sizeof(errbuf));
//         std::cerr << "avformat_open_input failed: " << errbuf << " (code: " << errorCode << ")\n";
//     }

//     if (errorCode != 0) {
//         qDebug() << "Cannot open file! avformat_open_input returned with " << errorCode;
//     }
//     errorCode = avformat_find_stream_info(format, nullptr);
//     if (errorCode != 0) {
//         qDebug() << "Cannot find stream info! avformat_find_stream_info returned with " << errorCode;
//     }

//     int audioStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
//     if (audioStream < 0) {
//         qDebug() << "Couldn't find audio stream! av_find_best_stream returned with " << audioStream;
//     }

//     const AVCodec *codec = avcodec_find_decoder(format->streams[audioStream]->codecpar->codec_id);
//     AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

//     avcodec_parameters_to_context(codec_ctx, format->streams[audioStream]->codecpar);
//     avcodec_open2(codec_ctx, codec, nullptr);

//     SwrContext* swr = nullptr;
//     AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
//     AVChannelLayout in_ch_layout = codec_ctx->ch_layout;

//     swr_alloc_set_opts2(&swr, &out_ch_layout,
//                         AV_SAMPLE_FMT_S16,
//                         48000,
//                         &in_ch_layout,
//                         codec_ctx->sample_fmt,
//                         codec_ctx->sample_rate,
//                         0, nullptr);

//     swr_init(swr);

//     AVPacket *packet = av_packet_alloc();
//     AVFrame *frame = av_frame_alloc();

//     while(av_read_frame(format, packet) >= 0) {

//         if(packet->stream_index != audioStream) {
//             av_packet_unref(packet);
//             continue;
//         }

//         if (avcodec_send_packet(codec_ctx, packet) < 0) {
//             av_packet_unref(packet);
//             continue;
//         }

//         while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
//             int out_samples = swr_get_out_samples(swr, frame->nb_samples);

//             uint8_t *out_buf = nullptr;
//             int out_buf_size = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 1);

//             out_buf = (uint8_t*)av_malloc(out_buf_size);

//             int samples_converted = swr_convert(
//                 swr,
//                 &out_buf,
//                 out_samples,
//                 (const uint8_t**)frame->data,
//                 frame->nb_samples
//             );

//             if (samples_converted > 0) {
//                 int actual_size = av_samples_get_buffer_size(
//                     nullptr, 2, samples_converted, AV_SAMPLE_FMT_S16,1
//                 );

//                 pcmData.insert(pcmData.end(), out_buf, out_buf + actual_size);
//             }

//             av_free(out_buf);
//             av_frame_unref(frame);
//         }

//         av_packet_unref(packet);
//     }

//     uint8_t *out_buf = nullptr;
//     int remaining = swr_get_out_samples(swr, 0);
//     if (remaining > 0) {
//         int out_buf_size = av_samples_get_buffer_size(nullptr, 2, remaining, AV_SAMPLE_FMT_S16, 1);
//         out_buf = (uint8_t*)av_malloc(out_buf_size);

//         int flushed = swr_convert(swr, &out_buf, remaining, nullptr, 0);
//         if (flushed >0) {
//             int actual_size = av_samples_get_buffer_size(
//                 nullptr, 2, flushed, AV_SAMPLE_FMT_S16, 1
//             );
//             pcmData.insert(pcmData.end(), out_buf, out_buf + actual_size);
//         }
//         av_free(out_buf);
//     }
// }

// void Mp3Encoder::saveFileAsPcm()
// {
//     FILE *f = fopen("output.pcm", "wb");
//     fwrite(pcmData.data(), 1, pcmData.size(), f);
//     fclose(f);
// }
