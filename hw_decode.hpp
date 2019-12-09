#ifndef HW_DECODE_HPP
#define HW_DECODE_HPP
/**
 * @file
 * HW-Accelerated decoding example.
 *
 * @example hw_decode.c
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <stdio.h>
#include <unistd.h>
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/hwcontext.h>
    #include <libavutil/opt.h>
    #include <libavutil/avassert.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <math.h>
#include <sys/timeb.h>
#include "frame_queue.h"
#include <list>

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    const enum AVPixelFormat tmp_pix_fmt = AVPixelFormat(119);
    // TODO(shiguang): get the pix format from global variation??
    // printf("ctx->fmt: %d\n", ctx->pix_fmt);
    for (p = pix_fmts; *p != -1; p++) {
        // printf("*p: %d\n", *p);
        if (*p == tmp_pix_fmt)
            return *p;
    }
    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

enum WriteType {
    TOPPM = 0,
    TOQUEUE
};

class HWDecoder {
    public:
        HWDecoder(const char* dt) {
            int ret = hwdevice_init(dt);            
        }
        ~HWDecoder() {
            av_buffer_unref(&hw_device_ctx);
        }
        int get_num_gops(const char* filename, int* num_gops);
        int get_gop_frame(const char* filename, int gop_target);
        int get_gop_frames(const char* filename, int* gop_targets, int frames);
        /* load key frames according gop_targets and frames. it will generate gop_targets inner the function if gop_targets==NULL */
        int load_gop_frames(const char* filename, int* gop_targets, int frames, link_queue q_frames);
        
    private:
        /* init the hw device */
        int hwdevice_init(const char* dt);
        /* init the hw context of decoder*/
        int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type);
        //enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
        /* decode the packet and write it to a ppm image */
        int decode_write(AVCodecContext *avctx, AVPacket *packet, const enum WriteType wtype=TOPPM, link_queue q_frames=NULL);
        /* save a frame(NV12) as a ppm image */
        void ppm_save(AVFrame *pFrame, char *filename);
        /* convert frame to buffer */
        void frame_to_qbuffer(AVFrame *pFrame, datatype* q_buffer);
        
        AVBufferRef *hw_device_ctx = NULL;
        enum AVPixelFormat hw_pix_fmt; // TODO(shiguang) could be used in multiprocessing??
        enum AVHWDeviceType hw_device_type;
};

int HWDecoder::hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type) {
    int err = 0;
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);    
    return err;
}
        
int HWDecoder::hwdevice_init(const char* dt) {
    hw_device_type = av_hwdevice_find_type_by_name(dt);
    if (hw_device_type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", dt);
        fprintf(stderr, "Available device types:");
        while((hw_device_type = av_hwdevice_iterate_types(hw_device_type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(hw_device_type));
        fprintf(stderr, "\n");
        return -1;
    }
     
    if (av_hwdevice_ctx_create(&hw_device_ctx, hw_device_type,
                                      NULL, NULL, 0) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return -1;
    }           
    printf("Using hw device type: %d\n", hw_device_type);
    return 0;
}

int HWDecoder::get_num_gops(const char* filename, int* num_gops) {
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    
    // TODO(shiguang): do not open the video repeated
    /* open the input file */
    if (avformat_open_input(&input_ctx, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", filename);
        return -1;
    }
    /* no necessary??
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }
    */

    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;
    
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(hw_device_type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_device_type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }
    // printf("hw pix fmt: %d\n", hw_pix_fmt);
    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;
    // TODO(shiguang): check
    decoder_ctx->get_format  = get_hw_format;
    
    if (hw_decoder_init(decoder_ctx, hw_device_type) < 0)
        return -1;
    
    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }
    
    AVPacket packet;
    *num_gops = 0;
    
    /* actual decoding and dump the raw data */
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if ((video_stream == packet.stream_index) && (packet.flags & AV_PKT_FLAG_KEY))            
            ++ (*num_gops);
            
        av_packet_unref(&packet);
    }

    // -- (*num_gops);    
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    return 0;
}

int HWDecoder::decode_write(AVCodecContext *avctx, AVPacket *packet, const enum WriteType wtype, link_queue q_frames)
{
    AVFrame *frame = NULL, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
    char buf[1024];
    int ret = 0;
    int frame_number;
    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    while (1) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            goto fail;           
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        if (frame->format == hw_pix_fmt) { //(119)
            /* retrieve data from GPU to CPU */
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;
        } else
            tmp_frame = frame; //AV_PIX_FMT_NV12(23)
        // printf("avctx->format: %d, frame->format: %d, tmp_frame->format: %d, sw_frame: %d, hw_pix_fmt: %d\n", avctx->pix_fmt, frame->format, tmp_frame->format, sw_frame->format, hw_pix_fmt);
        frame_number = avctx->frame_number;
        //printf("saving frame %3d\n", frame_number);
        // fflush(stdout);

        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "image_%d.pgm", frame_number);
        if (wtype == TOPPM) {
            ppm_save(tmp_frame, buf);
        }            
        else if (wtype == TOQUEUE) {
            datatype* q_buffer = (datatype*) malloc(sizeof(datatype));
            frame_to_qbuffer(tmp_frame, q_buffer);
            if (queue_en(q_frames, *q_buffer) == 0)
                printf("queue failed.\n");
        }    

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
		if (ret < 0)
	        return ret;
    }
}

void HWDecoder::ppm_save(AVFrame *pFrame, char *filename)
{
    
    uint8_t *buffer;
    AVFrame* pFrameRGB;
    
    int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);

    buffer = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t));
    if (buffer == NULL) {
        printf("av malloc failed\n");
        return;
    }
    
    pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL) {
        printf("av frame alloc failed\n");
        return;
    }
    avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_RGB24, pFrame->width, pFrame->height);
    
    struct SwsContext *img_convert_ctx;
    // TODO(shiguang): get cached context instead ??
    img_convert_ctx = sws_getContext(
            pFrame->width, pFrame->height, AV_PIX_FMT_NV12, 
            pFrame->width, pFrame->height, AV_PIX_FMT_RGB24,
            SWS_BICUBIC, NULL, NULL, NULL);

    sws_scale(img_convert_ctx, 
        pFrame->data, 
        pFrame->linesize, 0, pFrame->height,
        pFrameRGB->data, 
        pFrameRGB->linesize);
    sws_freeContext(img_convert_ctx);
    
	int width = pFrame->width;
    int linesize = pFrame->width * 3;
    int height = pFrame->height;
    int wrap = pFrameRGB->linesize[0];
        
    //printf("pFrame...width: %d, height: %d, bytes: %d, wrap: %d\n", pFrame->width, pFrame->height, numBytes, pFrame->linesize[0]);
    //printf("pFrameRGB...width: %d, height: %d, bytes: %d, wrap: %d\n", pFrameRGB->width, pFrameRGB->height, numBytes, pFrameRGB->linesize[0]);
    int stride_0 = height * linesize;
    int stride_1 = linesize;
    int stride_2 = 3;
    
    /*
    uint8_t *src  = buffer; //(uint8_t*) buffer[0];
    
    uint8_t *dest = (uint8_t*) (*arr)->data;
    
    int array_idx;
    if (cur_pos == pos_target) {
        array_idx = 1;
    } else {
        array_idx = 0;
    }
    memcpy(dest + array_idx * stride_0, src, height * linesize * sizeof(uint8_t));
    */
      
              
    FILE *fp;
    if ((fp = fopen(filename, "wb")) == NULL) {
        printf("open file %s failed!\n", filename);
        av_free(buffer);
        return;
    }

    fprintf(fp, "P6\n%d %d\n%d\n", width, height, 255);
    for (int y = 0; y < height; y++)
        fwrite(buffer + y * wrap, 1, linesize, fp);
     
    fclose(fp);   
    free(buffer);
}

void HWDecoder::frame_to_qbuffer(AVFrame *pFrame, datatype* q_buffer)
{
    AVFrame* pFrameRGB;
    // TODO(shiguang): parameterize dst_h and dst_w
    const int dst_h = 256, dst_w = 256;
    int numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, dst_w, dst_h);

    q_buffer->data = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t));
    if (q_buffer->data == NULL) {
        printf("av malloc failed\n");
        return;
    }

    pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL) {
        printf("av frame alloc failed\n");
        return;
    }
    avpicture_fill((AVPicture *)pFrameRGB, q_buffer->data, AV_PIX_FMT_RGB24, dst_w, dst_h);

    struct SwsContext *img_convert_ctx;
    // TODO(shiguang): get cached context instead ??
    img_convert_ctx = sws_getContext(
            pFrame->width, pFrame->height, AV_PIX_FMT_NV12, 
            dst_w, dst_h, AV_PIX_FMT_RGB24,
            SWS_BICUBIC, NULL, NULL, NULL);

    sws_scale(img_convert_ctx, 
        pFrame->data, 
        pFrame->linesize, 0, pFrame->height,
        pFrameRGB->data, 
        pFrameRGB->linesize);
    sws_freeContext(img_convert_ctx);
    
    int wrap = pFrameRGB->linesize[0];

    q_buffer->width = dst_w;
    q_buffer->height = dst_h;
    q_buffer->wrap = wrap;
    
    // Notice free buffer and q_buffer
}

int HWDecoder::get_gop_frame(const char* filename, int gop_target)
{
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    
    // TODO(shiguang): do not open the video repeated
    /* open the input file */
    if (avformat_open_input(&input_ctx, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", filename);
        return -1;
    }
    /* no necessary??
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }
    */

    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;
    
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(hw_device_type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_device_type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;
    // TODO(shiguang): check
    decoder_ctx->get_format  = get_hw_format;
    
    if (hw_decoder_init(decoder_ctx, hw_device_type) < 0)
        return -1;
    
    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }
    
    AVPacket packet;
    int gop_pos = -1;
    /* actual decoding and dump the raw data */
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if ((video_stream == packet.stream_index) && (packet.flags & AV_PKT_FLAG_KEY)) {
            gop_pos ++;
            if (gop_pos == gop_target) {
                ret = decode_write(decoder_ctx, &packet);
                ret = 0;
            //break;
            } 
        }        
           
        av_packet_unref(&packet);
    }
    
    printf("flush decoder...\n");
    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(decoder_ctx, &packet);
    av_packet_unref(&packet);
    
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    return 0;
}

int HWDecoder::get_gop_frames(const char* filename, int* gop_targets, int frames)
{
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    
    // TODO(shiguang): do not open the video repeated
    /* open the input file */
    if (avformat_open_input(&input_ctx, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", filename);
        return -1;
    }
    /* no necessary??
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }
    */

    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;
    
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(hw_device_type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_device_type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;
    // TODO(shiguang): check
    decoder_ctx->get_format  = get_hw_format;
    
    if (hw_decoder_init(decoder_ctx, hw_device_type) < 0)
        return -1;
    
    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }
    
    AVPacket packet;
    int gop_pos = -1;
    int g = 0;
    /* actual decoding and dump the raw data */
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if ((video_stream == packet.stream_index) && (packet.flags & AV_PKT_FLAG_KEY)) {
            gop_pos ++;

            // output the remains key frame after the gop_targets[-1]
            if (g >= frames) {
                ret = decode_write(decoder_ctx, &packet);
                ret = 0;
                // printf("gop_pos: %d\n", gop_pos);
                break;
            } 
            // !!! Note that gop_targets[g] <= gop_targets[g+1]
            while (gop_pos == gop_targets[g] && g < frames) {                
                ret = decode_write(decoder_ctx, &packet);
                ret = 0;
                g++;
                // printf("gop_pos: %d\n", gop_pos);
            }
               
        }        
           
        av_packet_unref(&packet);
    }
    if (g != frames) {
        fprintf(stderr, "g != frames");
    }        
    printf("flush decoder...\n");
    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(decoder_ctx, &packet);
    av_packet_unref(&packet);
    
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    return 0;
}

int HWDecoder::load_gop_frames(const char* filename, int* gop_targets, int frames, link_queue q_frames)
{
    AVFormatContext *input_ctx = NULL;
    int video_stream, ret;
    AVStream *video = NULL;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    
    // TODO(shiguang): do not open the video repeated
    /* open the input file */
    if (avformat_open_input(&input_ctx, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", filename);
        return -1;
    }
    /* no necessary??
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }
    */

    /* find the video stream information */
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    video_stream = ret;
    
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    decoder->name, av_hwdevice_get_type_name(hw_device_type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_device_type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    video = input_ctx->streams[video_stream];
    if (avcodec_parameters_to_context(decoder_ctx, video->codecpar) < 0)
        return -1;
    // TODO(shiguang): check
    decoder_ctx->get_format  = get_hw_format;
    
    if (hw_decoder_init(decoder_ctx, hw_device_type) < 0)
        return -1;
    
    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream);
        return -1;
    }
    
    AVPacket packet;
    
    /* generate gop_target if there is no gop_target supplied */
    if (gop_targets == NULL) {
        int num_gops = 0;
        while (ret >= 0) {
            if ((ret = av_read_frame(input_ctx, &packet)) < 0)
                break;
            if ((video_stream == packet.stream_index) && (packet.flags & AV_PKT_FLAG_KEY))            
                ++ num_gops;		        
            av_packet_unref(&packet);
        }
        frames = frames < num_gops ? frames : num_gops;
        gop_targets = new int[frames];
        float seg_size = float(num_gops) / frames;
        for (int i=0; i<frames; ++i) {
	        gop_targets[i] = floor(i*seg_size);
        }    
    }
        
    int gop_pos = -1;
    int g = 0;
    ret = 0;
    av_seek_frame(input_ctx, video_stream, 0, AVSEEK_FLAG_BACKWARD);
    /* actual decoding and dump the raw data */
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if ((video_stream == packet.stream_index) && (packet.flags & AV_PKT_FLAG_KEY)) {
            gop_pos ++;
			/*
            // output the remains key frame after the gop_targets[-1]
            if (g >= frames) {
                ret = decode_write(decoder_ctx, &packet, TOQUEUE, q_frames);
                ret = 0;
                // printf("gop_pos: %d\n", gop_pos);
                break;
            } 
            */
            // !!! Note that gop_targets[g] <= gop_targets[g+1]
            while (gop_pos == gop_targets[g] && g < frames) {                
                ret = decode_write(decoder_ctx, &packet, TOQUEUE, q_frames);
                ret = 0;
                g++;
                // printf("gop_pos: %d\n", gop_pos);
            }
               
        }        
           
        av_packet_unref(&packet);
    }
    if (g != frames) {
        fprintf(stderr, "g != frames");
    }        
    // printf("flush decoder...\n");
    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(decoder_ctx, &packet, TOQUEUE, q_frames);
    av_packet_unref(&packet);
    
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);

    return 0;
}
#endif
