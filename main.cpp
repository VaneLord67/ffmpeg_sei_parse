#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <io.h>
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavcodec/bsf.h>
    #include <libswscale/swscale.h>
}

//#define DEV

enum class MessageType : uint8_t {
    PARAMETER = 0,
    IMAGE_FRAME = 1,
    SEI = 2,
    END = 3
};

char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

int main(int argc, char* argv[]) {
    // 设置stdout为二进制模式，否则程序会转义图像数据造成接收端画面产生【扭曲/偏移】异常
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#else
    freopen(NULL, "wb", stdout);
#endif
    AVFormatContext* fmt_ctx = NULL;
    AVCodecContext* codec_ctx = NULL;
    const AVCodec* codec = NULL;
    const size_t message_type_size = 1;
    const size_t parameter_type_size = 4;
    const size_t sei_len_size = 4;
    int ret;
    //const char* url = "E:/rtsp/rtsp.sei.flv";
#ifdef DEV
    const char* url = "rtmp://localhost/live/livestream";
#else
    if (argc < 2) {
        std::cerr << "argc mismatch" << std::endl;
        std::cerr << "Usage: ./ffmpeg_sei_parse rtmp_url" << std::endl;
        return 1;
    }
    const char* url = argv[1];
#endif
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

    AVCodecParameters* codecParams = fmt_ctx->streams[video_stream_index]->codecpar;
    uint32_t width = codecParams->width;
    uint32_t height = codecParams->height;
    MessageType message_type = MessageType::PARAMETER;
    fwrite(&message_type, message_type_size, 1, stdout);
    // 将宽度高度传递给接收方，以便后续知道每一帧要读取多少字节的图片数据
    fwrite(&width, parameter_type_size, 1, stdout);
    fwrite(&height, parameter_type_size, 1, stdout);

    codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        return 1;
    }

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
    SwsContext* sws_ctx = NULL;
    message_type = MessageType::IMAGE_FRAME;
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            AVFrame* frame = av_frame_alloc();
            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet to decoder: %s\n", av_err2str(ret));
                goto end;
            }
            while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                AVFrameSideData* sei_sd = av_frame_get_side_data(frame, AV_FRAME_DATA_SEI_UNREGISTERED);
                if (sei_sd) {
                    message_type = MessageType::SEI;
                    fwrite(&message_type, message_type_size, 1, stdout);
                    uint32_t sei_len = strlen((char*)(sei_sd->data + 16));
                    fwrite(&sei_len, sei_len_size, 1, stdout);
                    fwrite(sei_sd->data + 16, sei_len, 1, stdout);
                    fflush(stdout);
                    //fprintf(stdout, "SEI Data:\n");
                    //std::cout << sei_sd->data + 16 << std::endl;
                }
                if (!sws_ctx) {
                    sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                        frame->width, frame->height, AV_PIX_FMT_BGR24,
                        SWS_BILINEAR, NULL, NULL, NULL);
                    if (!sws_ctx) {
                        fprintf(stderr, "Failed to initialize SwsContext for image conversion\n");
                        goto end;
                    }
                }
                AVFrame* bgr_frame = av_frame_alloc();
                bgr_frame->width = frame->width;
                bgr_frame->height = frame->height;
                bgr_frame->format = AV_PIX_FMT_BGR24;
                ret = av_frame_get_buffer(bgr_frame, 32);
                if (ret < 0) {
                    fprintf(stderr, "Failed to allocate frame data\n");
                    av_frame_free(&bgr_frame);
                    goto end;
                }
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                    bgr_frame->data, bgr_frame->linesize);
                message_type = MessageType::IMAGE_FRAME;
                fwrite(&message_type, message_type_size, 1, stdout);
                fwrite(bgr_frame->data[0], 3, (size_t)bgr_frame->width * bgr_frame->height, stdout);
                fflush(stdout);
                av_frame_free(&bgr_frame);
            }
            av_frame_free(&frame);
            av_packet_unref(pkt);
        }
        av_packet_unref(pkt);
    }
end:
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    message_type = MessageType::END;
    fwrite(&message_type, message_type_size, 1, stdout);
    return 0;
}
