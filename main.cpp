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
    //const char* url = "rtmp://localhost/live/livestream";
    const char* url = "E:/rtsp/rtsp_30.flv";

    const char* h264url = "E:/rtsp/rtsp_30.h264";
    AVCodecContext* h264_codec_ctx = NULL;
    const AVCodec* h264_codec = NULL;
    AVFormatContext* h264_fmt_ctx = NULL;
    ret = avformat_open_input(&h264_fmt_ctx, h264url, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening input file%s\n", av_err2str(ret));
        return 1;
    }
    ret = avformat_find_stream_info(h264_fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error finding stream info: %s\n", av_err2str(ret));
        return 1;
    }
    int h264_video_stream_index = -1;
    for (unsigned int i = 0; i < h264_fmt_ctx->nb_streams; i++) {
        if (h264_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            h264_video_stream_index = static_cast<int>(i);
            break;
        }
    }
    if (h264_video_stream_index == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        avformat_close_input(&h264_fmt_ctx);
        return -1;
    }
    h264_codec = avcodec_find_decoder(h264_fmt_ctx->streams[h264_video_stream_index]->codecpar->codec_id);
    if (!h264_codec) {
        fprintf(stderr, "Codec not found\n");
        return 1;
    }
    h264_codec_ctx = avcodec_alloc_context3(h264_codec);
    if (!h264_codec_ctx) {
        fprintf(stderr, "Could not allocate codec context\n");
        return 1;
    }
    ret = avcodec_parameters_to_context(h264_codec_ctx, h264_fmt_ctx->streams[h264_video_stream_index]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Error setting codec parameters: %s\n", av_err2str(ret));
        return 1;
    }
    ret = avcodec_open2(h264_codec_ctx, h264_codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening codec: %s\n", av_err2str(ret));
        return 1;
    }

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

    AVBSFContext* bsf = nullptr;
    const AVBitStreamFilter* filter;
    // h264 sei filter
    filter = av_bsf_get_by_name("h264_mp4toannexb");
    if (!filter) {
        printf("h264_mp4toannexb bitstream filter error\n");
        return 1;
    }

    ret = av_bsf_alloc(filter, &bsf);
    if (ret < 0) {
        fprintf(stderr, "错误分配BSF上下文\n");
        return 1;
    }

    ret = avcodec_parameters_copy(bsf->par_in, fmt_ctx->streams[video_stream_index]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "错误复制编解码器参数到BSF输入\n");
        return 1;
    }

    ret = av_bsf_init(bsf);
    if (ret < 0) {
        fprintf(stderr, "错误初始化BSF\n");
        return 1;
    }

    av_dump_format(fmt_ctx, video_stream_index, url, 0);

    AVPacket *pkt = av_packet_alloc();
    pkt->data = NULL;
    pkt->size = 0;
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            uint8_t* data = pkt->data;
            int size = pkt->size;
            int nal_type;

            // 解析H.264 NALU类型
            nal_type = data[4] & 0x1F;  // 假设NALU头部长度为4字节
            std::cout << "nal_type: " << nal_type << std::endl;
            // 根据NALU类型执行相应的操作
            switch (nal_type) {
            case 1:
                // NALU类型为非IDR图像的片
                std::cout << "not IDR" << std::endl;
                break;
            case 5:
                // NALU类型为IDR图像的片
                std::cout << "IDR" << std::endl;
                break;
                // 其他类型的NALU，您可以根据需要添加更多的情况
            default:
                break;
            }


            ret = av_bsf_send_packet(bsf, pkt);
            if (ret < 0) {
                fprintf(stderr, "错误发送数据包到BSF\n");
                av_packet_unref(pkt);
                continue;
            }
            // 获取处理过的数据包
            while ((ret = av_bsf_receive_packet(bsf, pkt)) == 0) {
                AVFrame* frame = av_frame_alloc();
                ret = avcodec_send_packet(h264_codec_ctx, pkt);
                if (ret < 0) {
                    fprintf(stderr, "Error sending packet to decoder: %s\n", av_err2str(ret));
                    return 1;
                }
                while ((ret = avcodec_receive_frame(h264_codec_ctx, frame)) >= 0) {
                    AVFrameSideData* sei_sd = av_frame_get_side_data(frame, AV_FRAME_DATA_SEI_UNREGISTERED);
                    if (sei_sd) {
                        printf("SEI Data:\n");
                        std::cout << sei_sd->data + 16 << std::endl;
                        //printf("%.*s\n", (unsigned int)sei_sd->size, sei_sd->data);
                        //fprintf(stdout, "side data %d: %s\n", sei_sd->type, sei_sd->data + 16);
                    }
                    av_frame_free(&frame);
                }
                //ret = avcodec_receive_frame(h264_codec_ctx, frame);
                //if (ret >= 0) {
                //    AVFrameSideData* sei_sd = av_frame_get_side_data(frame, AV_FRAME_DATA_SEI_UNREGISTERED);
                //    if (sei_sd) {
                //        printf("SEI Data:\n");
                //        std::cout << sei_sd->data + 16 << std::endl;
                //        //printf("%.*s\n", (unsigned int)sei_sd->size, sei_sd->data);
                //        //fprintf(stdout, "side data %d: %s\n", sei_sd->type, sei_sd->data + 16);
                //    }
                //    //printf("BGR24 Data:\n");
                //    //for (int y = 0; y < frame->height; y++) {
                //    //    for (int x = 0; x < frame->width; x++) {
                //    //        uint8_t* ptr = frame->data[0] + y * frame->linesize[0] + x * 3;
                //    //        printf("(%d, %d, %d) ", ptr[0], ptr[1], ptr[2]);
                //    //    }
                //    //    printf("\n");
                //    //}
                //}
                //av_frame_free(&frame);
                
                av_packet_unref(pkt);
            }
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                fprintf(stderr, "错误接收数据包从BSF\n");
                av_packet_unref(pkt);
                break;
            }


        }
        av_packet_unref(pkt);
    }

    avcodec_free_context(&codec_ctx);
    avcodec_free_context(&h264_codec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}
