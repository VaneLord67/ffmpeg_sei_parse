#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavcodec/bsf.h>
}

char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

int main(int argc, char* argv[]) {
    AVFormatContext* fmt_ctx = NULL;
    AVCodecContext* codec_ctx = NULL;
    const AVCodec* codec = NULL;
    int ret;
    const char* url = "rtmp://localhost/live/livestream";
    //const char* url = "E:/rtsp/rtsp.sei.flv";

    avformat_network_init();
    ret = avformat_open_input(&fmt_ctx, url, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening input file%s\n", av_err2str(ret));
        return 1;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error finding stream info: %s\n", av_err2str(ret));
        return 1;
    }

    int video_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = static_cast<int>(i);
            break;
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        return 1;
    }
    std::cout << "codec_id: " << fmt_ctx->streams[video_stream_index]->codecpar->codec_id << std::endl;

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate codec context\n");
        return 1;
    }

    ret = avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Error setting codec parameters: %s\n", av_err2str(ret));
        return 1;
    }

    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening codec: %s\n", av_err2str(ret));
        return 1;
    }

    av_dump_format(fmt_ctx, video_stream_index, url, 0);

    AVPacket *pkt = av_packet_alloc();
    pkt->data = NULL;
    pkt->size = 0;
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            AVFrame* frame = av_frame_alloc();
            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet to decoder: %s\n", av_err2str(ret));
                return 1;
            }
            ret = avcodec_receive_frame(codec_ctx, frame);
            AVFrameSideData* sei_sd = av_frame_get_side_data(frame, AV_FRAME_DATA_SEI_UNREGISTERED);
            if (sei_sd) {
                printf("SEI Data:\n");
                std::cout << sei_sd->data + 16 << std::endl;
            }
            av_frame_free(&frame);
            av_packet_unref(pkt);
        }
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            fprintf(stderr, "错误接收数据包从BSF\n");
            av_packet_unref(pkt);
            break;
        }
        av_packet_unref(pkt);
    }
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return 0;
}
