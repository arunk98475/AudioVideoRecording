#define __STDC_CONSTANT_MACROS
#include<iostream>
#include<string.h>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
using namespace std;

class Timer
{
    chrono::time_point<chrono::system_clock> start;
public:

    Timer() {};
    void StartTimer()
    {
        start = chrono::system_clock::now();
    };

    __int64 ElapsedTime()
    {
        return chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - start).count();
    }
    __int64 ElapsedTimeM()
    {
        return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - start).count();
    }
};
enum MediaSettings
{
    UNKNOWN = -1,
    VIDEO_DEFAULT,
    AUDIO_DEFAULT,
    AUDIO_FOR_VIDEO,
    VIDEO_CUSTOM,
    AUDIO_CUSTOM
};
class RecordVars
{
    const char* m_strFrameR;
    int m_iFrameRate;
    const char* m_crf;
public:
    const AVOutputFormat* ofmt;
    const AVInputFormat* ifmt;
    AVFormatContext* ifmt_ctx;
    static AVFormatContext* ofmt_ctx;
    static mutex m_Mutex;
    static bool m_Ready;
    condition_variable cv;
    static bool m_bLoopBreaker;
    Timer* m_Timer;
    AVCodecParameters* av_codec_par_in;
    AVCodecParameters* av_codec_par_out;
    AVCodecContext* av_cntx_in;
    AVCodecContext* av_cntx_out;
    const AVCodec* av_codec_in;
    const AVCodec* av_codec_out;
    AVStream* av_stream;
    AVDictionary* options;
    AVPacket* av_pkt_in;
    AVPacket* av_pkt_out;
    AVFrame* av_frm_in;
    AVFrame* av_frm_out;
    AVAudioFifo* fifo;
    int stream_idx;
    SwsContext* swsCtx;
    SwrContext* resampleContext;
    MediaSettings* m_MedSettings;
    RecordVars(MediaSettings* medSett, string frameRate = "24");
    void OpenInputMedia(const char* shortName, const char* mediaFormat);
    void FindMediaStreamIndex(AVMediaType av_media_type);
    void SetOutPutFilename(const char* short_name = NULL, const char* filename = NULL, const char* mime_type = NULL);
    void SetOutputCodecContext(AVDictionary* dictOptions = NULL);
    ~RecordVars();
    void SetOutPutFormatContext(AVFormatContext* outFmtCtx);
    void writeFormatHeader();
    void SetVideoSwsContext();
    void SetAudioSwrContext();
    static void VideoAudioThreadDriver(RecordVars* video, RecordVars* audio);
    int add_samples_to_fifo(uint8_t** converted_input_samples, const int frame_size);
    void SetMaxTimeofRec(int sec);
    void ScreenCapThread();
    void SetCrFValue(string crf);



private:
    void ApplyDefaultVideoInputSettings();
    void ApplyDefaultAudioInputSettings(AVCodecID codecType);
    const char* CombineConstChars(const char* firstChar, const char* secdChar);
    int GOP_SIZE;
    int MAX_B_FRAMES;
    int TIME_BASE_NUM;
    int TIME_BASE_DEN;
    const char* OUT_PUT_FILE_NAME;
    __int64 m_iMaxTime;

    void MicCaptureThread();
    int initConvertedSamples(uint8_t*** converted_input_samples, AVCodecContext* output_codec_context, int frame_size);
};

AVFormatContext* RecordVars::ofmt_ctx = NULL;
bool RecordVars::m_bLoopBreaker = false;
mutex RecordVars::m_Mutex;
bool RecordVars::m_Ready = false;

RecordVars::RecordVars(MediaSettings* medSett, string frameRate)
{
    avdevice_register_all();
    m_strFrameR = frameRate.c_str();
    m_iFrameRate = atoi(m_strFrameR);
    m_Timer = new Timer();
    ofmt = NULL;
    ifmt = NULL;
    options = NULL;
    av_codec_par_in = avcodec_parameters_alloc();
    if (av_codec_par_in == NULL)
    {
        throw exception("Unable to allocate input codec ");
    }
    m_iMaxTime = 20;
    av_codec_par_out = avcodec_parameters_alloc();
    if (av_codec_par_out == NULL)
    {
        throw exception("Unable to allocate output codec ");
    }
    av_cntx_in = NULL;
    av_cntx_out = NULL;
    av_stream = NULL;
    stream_idx = -1;

    av_frm_out = av_frame_alloc();
    if (av_frm_out == NULL)
    {
        throw exception("Unable to allocate output frame ");
    }
    av_pkt_in = av_packet_alloc();
    if (av_pkt_in == NULL)
    {
        throw exception("Unable to allocate input packet ");
    }
    memset(av_pkt_in, 0, sizeof(AVPacket));
    av_frm_in = av_frame_alloc();
    if (av_frm_in == NULL)
    {
        throw exception("Unable to allocate input frame ");
    }
    av_pkt_out = av_packet_alloc();
    if (av_pkt_out == NULL)
    {
        throw exception("Unable to allocate output packet ");
    }
    if (*medSett == UNKNOWN)
    {
        throw invalid_argument("Unknown Media Input settings");
    }
    else if (*medSett == VIDEO_DEFAULT)
    {
        m_MedSettings = medSett;
        ApplyDefaultVideoInputSettings();
    }
    else if (*medSett == AUDIO_FOR_VIDEO)
    {

        m_MedSettings = medSett;
        ApplyDefaultAudioInputSettings(AV_CODEC_ID_AAC);
    }
    else if (*medSett == AUDIO_DEFAULT)
    {
        m_MedSettings = medSett;
        ApplyDefaultAudioInputSettings(AV_CODEC_ID_MP3);
    }
    else if (*medSett == VIDEO_CUSTOM || *medSett == AUDIO_CUSTOM)
    {
        m_MedSettings = medSett;
    }
    else
    {
        throw invalid_argument("Please in put valid parameter");
    }

}

RecordVars::~RecordVars()
{
    m_Ready = false;
    m_bLoopBreaker = false;
    avcodec_free_context(&av_cntx_out);
    if (ifmt_ctx)
    {
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ifmt_ctx);
    }
    if (ofmt_ctx)
    {
        avio_close(ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        ofmt_ctx = NULL;
    }
    if (m_Timer)
    {
        delete m_Timer;
    }
    if (av_codec_par_out)
    {
        avcodec_parameters_free(&av_codec_par_out);
    }
    if (av_cntx_in)
    {
        avcodec_free_context(&av_cntx_in);
    }
    if (av_cntx_out)
    {
        avcodec_free_context(&av_cntx_out);
    }

    if (options)
    {
        av_dict_free(&options);
    }
    if (av_pkt_in)
    {
        av_packet_unref(av_pkt_in);
    }
    if (av_pkt_out)
    {
        av_packet_unref(av_pkt_out);
    }
    if (av_frm_in)
    {
        av_frame_free(&av_frm_in);
    }
    if (av_frm_out)
    {
        av_frame_free(&av_frm_out);
    }
    if (fifo)
    {
        av_audio_fifo_free(fifo);
    }

    if (swsCtx)
    {
        sws_freeContext(swsCtx);
    }
    if (resampleContext)
    {
        swr_free(&resampleContext);
    }
}

void RecordVars::SetOutPutFormatContext(AVFormatContext* outFmtCtx)
{
    ofmt_ctx = outFmtCtx;
}

void RecordVars::ApplyDefaultVideoInputSettings()
{
    av_codec_par_out->height = 840;
    av_codec_par_out->width = 1120;
    av_codec_par_out->bit_rate = 900000;
    av_codec_par_out->codec_id = AV_CODEC_ID_H264;
    av_codec_par_out->codec_type = AVMEDIA_TYPE_VIDEO;
    av_codec_par_out->format = AV_PIX_FMT_YUV420P;
    av_codec_par_out->sample_aspect_ratio.den = 3;
    av_codec_par_out->sample_aspect_ratio.num = 0;
    GOP_SIZE = m_iFrameRate;
    MAX_B_FRAMES = 2;
    TIME_BASE_NUM = 1;
    TIME_BASE_DEN = m_iFrameRate;
    if (av_dict_set(&options, "framerate", m_strFrameR, 0) < 0)
    {
        throw exception("Unable to set default frame rate for video input");
    }
    if (av_dict_set(&options, "probesize", "42M", 0) < 0)
    {
        throw exception("Unable to set default video size for video input");
    }
    if (av_dict_set(&options, "preset", "ultrafast", 0) < 0)
    {
        throw exception("Unable to set ultrafast video capture option");
    }
}

void RecordVars::ApplyDefaultAudioInputSettings(AVCodecID codecType)
{
    av_codec_par_out->bit_rate = 96000;
    av_codec_par_out->codec_id = codecType;
    av_codec_par_out->codec_type = AVMEDIA_TYPE_AUDIO;
    av_codec_par_out->format = AV_SAMPLE_FMT_FLTP;
    av_codec_par_out->profile = FF_PROFILE_AAC_LOW;
    if (av_dict_set(&options, "sample_rate", "44100", 0) < 0)
    {
        throw exception("Error: cannot set sample rate in input option");
    }
    if (av_dict_set(&options, "async", "1", 0) < 0)
    {
        throw exception("Error: cannot set async in input option");
    }
    if (av_dict_set(&options, "channels", "2", 0) < 0)
    {
        throw exception("Error: cannot set channels in input option");
    }
}

const char* RecordVars::CombineConstChars(const char* firstChar, const char* secdChar)
{
    char* res = new char[strlen(firstChar) + strlen(secdChar) + 1];
    int size = strlen(firstChar) + strlen(secdChar) + 1;
    char buffer[256];
    strcpy_s(buffer, firstChar);
    strcat_s(buffer, secdChar);
    return buffer;
}

void RecordVars::OpenInputMedia(const char* shortName, const char* mediaFormat)
{
    ifmt = av_find_input_format(shortName);
    if (ifmt == NULL)
    {
        throw exception(CombineConstChars("Unable to find input :", shortName));
    }
    if (avformat_open_input(&ifmt_ctx, mediaFormat, ifmt, &options) < 0)
    {
        throw exception(CombineConstChars("Unable to find media format :", mediaFormat));
    }
}

void RecordVars::FindMediaStreamIndex(AVMediaType av_media_type)
{
    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0)
    {
        throw exception("Error in Finding Stream");
    }

    for (int i = 0; i < (int)ifmt_ctx->nb_streams; i++)
    {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == av_media_type)
        {
            stream_idx = i;
            break;
        }

    }
    if (stream_idx == -1)
    {
        throw exception("Type of stream in parameter has not found");
    }
    av_codec_par_in = ifmt_ctx->streams[stream_idx]->codecpar;
    if (*m_MedSettings == AUDIO_DEFAULT || *m_MedSettings == AUDIO_FOR_VIDEO)
    {
        av_codec_par_out->sample_rate = av_codec_par_in->sample_rate;
        av_channel_layout_default(&av_codec_par_out->ch_layout, av_codec_par_in->ch_layout.nb_channels);
        if (*m_MedSettings == AUDIO_FOR_VIDEO)
            av_codec_par_out->frame_size = 1024 * 16 * av_codec_par_in->ch_layout.nb_channels;
    }
    av_codec_in = avcodec_find_decoder(av_codec_par_in->codec_id);
    if (av_codec_in == NULL)
    {
        throw exception("Unable to find input codec");
    }
    av_cntx_in = avcodec_alloc_context3(av_codec_in);
    if (av_cntx_in == NULL)
    {
        throw exception("Unable to allocate input codec context");
    }
    if (avcodec_parameters_to_context(av_cntx_in, av_codec_par_in) < 0)
    {
        throw exception("Unable to copy input codec parameters to input codec context");
    }
    AVDictionary* codec_options_in(0);
    if (*m_MedSettings == VIDEO_DEFAULT)
    {
        av_dict_set(&codec_options_in, "preset", "ultrafast", 0);
        av_dict_set(&codec_options_in, "rtbufsize", "1500M", 0);
        av_dict_set(&codec_options_in, "crf", m_crf, 0);
    }

    if (avcodec_open2(av_cntx_in, av_codec_in, &codec_options_in) < 0)
    {
        throw exception("Unable to Initialize the AVCodecContext to use the given AVCodec.");
    }
    av_codec_par_out->height = av_codec_par_in->height;
    av_codec_par_out->width = av_codec_par_in->width;
}

void RecordVars::SetOutPutFilename(const char* short_name, const char* filename, const char* mime_type)
{
    ofmt = av_guess_format(short_name, filename, mime_type);
    OUT_PUT_FILE_NAME = filename;
    if (ofmt == NULL)
    {
        throw exception("No matching format found . Please try with correct file extension.");
    }
    if (ofmt_ctx == NULL)
    {
        if (avformat_alloc_output_context2(&ofmt_ctx, ofmt, NULL, filename) < 0)
        {
            throw exception("Error in allocating av format output context");
        }
    }
    av_codec_out = avcodec_find_encoder(av_codec_par_out->codec_id);
    av_cntx_out = avcodec_alloc_context3(av_codec_out);

    if (av_codec_out == NULL)
    {
        throw exception("Unable to find an encoder");
    }
    av_stream = avformat_new_stream(ofmt_ctx, av_codec_out);
    if (!av_stream)
    {
        throw exception("Unable to find the corresponding stream");
    }

    if (!av_cntx_out)
    {
        throw exception("error in allocating the codec contexts");
    }

    if (avcodec_parameters_to_context(av_cntx_out, av_codec_par_out) < 0)
    {
        throw exception("error in converting the codec contexts");
    }
    if (*m_MedSettings == VIDEO_DEFAULT)
    {
        av_cntx_out->gop_size = GOP_SIZE;
        av_cntx_out->max_b_frames = MAX_B_FRAMES;
        av_cntx_out->time_base.num = TIME_BASE_NUM;
        av_cntx_out->time_base.den = TIME_BASE_DEN;
        av_cntx_out->ticks_per_frame = 1;
    }
    else if (*m_MedSettings == AUDIO_DEFAULT || *m_MedSettings == AUDIO_FOR_VIDEO)
    {
        av_cntx_out->time_base.num = 1;
        av_cntx_out->time_base.den = av_codec_par_in->sample_rate;
        av_cntx_out->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    }
    if (*m_MedSettings == AUDIO_FOR_VIDEO)
    {
        if ((av_codec_out)->supported_samplerates)
        {
            av_cntx_out->sample_rate = (av_codec_out)->supported_samplerates[0];
            for (int i = 0; (av_codec_out)->supported_samplerates[i]; i++)
            {
                if ((av_codec_out)->supported_samplerates[i] == av_cntx_in->sample_rate)
                {
                    av_cntx_out->sample_rate = av_cntx_in->sample_rate;
                }
            }
        }
    }
    if (avcodec_parameters_copy(av_stream->codecpar, av_codec_par_out) < 0)
    {
        throw exception("Codec parameter canot copied");
    }
}

void RecordVars::SetOutputCodecContext(AVDictionary* dictOptions)
{
    if (avcodec_open2(av_cntx_out, av_codec_out, &dictOptions) < 0)
    {
        throw exception("Unable to open the av output codec context");
    }


}
void RecordVars::writeFormatHeader()
{
    if (avio_open(&ofmt_ctx->pb, OUT_PUT_FILE_NAME, AVIO_FLAG_READ_WRITE) < 0)
    {
        throw exception("Unable to Create and initialize a AVIOContext");
    }
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        av_cntx_out->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (*m_MedSettings != AUDIO_FOR_VIDEO)
    {
        if (avformat_write_header(ofmt_ctx, NULL) < 0)
        {
            throw exception("Unable to write format header");
        }
    }
}

void RecordVars::SetVideoSwsContext()
{
    av_frm_in->width = av_cntx_in->width;
    av_frm_in->height = av_cntx_in->height;
    av_frm_in->format = av_codec_par_in->format;
    av_frm_out->width = av_cntx_out->width;
    av_frm_out->height = av_cntx_out->height;
    av_frm_out->format = av_codec_par_out->format;
    if (av_frame_get_buffer(av_frm_in, 0) < 0)
    {
        throw exception("Unable allocate input frame buffer");
    }
    if (av_frame_get_buffer(av_frm_out, 0) < 0)
    {
        throw exception("Unable allocate output frame buffer");
    }
    swsCtx = sws_alloc_context();
    if (sws_init_context(swsCtx, NULL, NULL) < 0)
    {
        throw exception("Unable to Initialize the swscaler context sws_context.");
    }
    swsCtx = sws_getContext(av_cntx_in->width, av_cntx_in->height, av_cntx_in->pix_fmt,
        av_cntx_out->width, av_cntx_out->height, av_cntx_out->pix_fmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (swsCtx == NULL)
    {
        throw exception("Cannot allocate SWC Context");
    }
}

void RecordVars::SetAudioSwrContext()
{
    fifo = av_audio_fifo_alloc(av_cntx_out->sample_fmt, av_cntx_out->ch_layout.nb_channels, 1);
    if (fifo == NULL)
    {
        throw exception("Could not allocate FIFOn");
    }
    resampleContext = NULL;
    AVChannelLayout outChannelLayout, inChannelLayout;
    av_channel_layout_default(&outChannelLayout, av_cntx_out->ch_layout.nb_channels);
    av_channel_layout_default(&inChannelLayout, av_cntx_in->ch_layout.nb_channels);
    int res = swr_alloc_set_opts2
    (
        &resampleContext,
        &outChannelLayout,
        av_cntx_out->sample_fmt,
        av_cntx_out->sample_rate,
        &inChannelLayout,
        av_cntx_in->sample_fmt,
        av_cntx_in->sample_rate,
        0,
        nullptr
    );
    if (resampleContext == NULL)
    {
        throw exception("Cannot allocate the resample context");
    }
    if ((swr_init(resampleContext)) < 0)
    {
        swr_free(&resampleContext);
        throw exception("Could not open resample contextn");
    }
}

void RecordVars::VideoAudioThreadDriver(RecordVars* video, RecordVars* audio)
{
    if (video != NULL && audio != NULL)
    {
        vector<thread> threads;
        threads.push_back(thread(&RecordVars::MicCaptureThread, audio));
        threads.push_back(thread(&RecordVars::ScreenCapThread, video));

        for (auto& thread : threads) {
            thread.join();
        }
    }
    else if (video != NULL)
    {
        thread tV(&RecordVars::ScreenCapThread, video);
        tV.join();
    }
    else if (audio != NULL)
    {
        thread tA(&RecordVars::MicCaptureThread, audio);
        tA.join();
    }
    else
    {
        return;
    }

}


void RecordVars::ScreenCapThread()
{
    int ii = 0;
    int enc_packet_counter = 0;
    m_Timer->StartTimer();
    int frameFinished;
    //int got_picture;
    int frame_index = 0;
    av_pkt_out = av_packet_alloc();
    int j = 0;
    while (av_read_frame(ifmt_ctx, av_pkt_in) >= 0)
    {
        if (m_Timer->ElapsedTimeM() >= m_iMaxTime * 1000)
        {
            RecordVars::m_bLoopBreaker = true;
            break;
        }
        int pts = m_Timer->ElapsedTimeM() * 90;
        if (RecordVars::m_bLoopBreaker)
            break;
        int ret = avcodec_send_packet(av_cntx_in, av_pkt_in);
        if (ret < 0)
        {
            printf("Error while sending packet");
        }

        frameFinished = true;
        int response = 0;
        response = avcodec_receive_frame(av_cntx_in, av_frm_in);

        if (response < 0)
        {
            printf("Error while receiving frame from decoder");
            frameFinished = false;
        }

        if (frameFinished)
        {
            memset(av_pkt_out, 0, sizeof(AVPacket)); //???

            av_pkt_out->data = NULL;
            av_pkt_out->size = 0;
            av_pkt_out->pts = pts;

            av_pkt_out->dts = AV_NOPTS_VALUE;
            av_pkt_out->duration = av_rescale_q(1, av_cntx_out->time_base, av_stream->time_base);

            av_frm_out->pts = pts;
            av_frm_out->duration = av_rescale_q(1, av_cntx_out->time_base, av_stream->time_base);
            enc_packet_counter++;


            int sts = sws_scale(swsCtx,
                av_frm_in->data,
                av_frm_in->linesize,
                0,
                av_frm_in->height,
                av_frm_out->data,
                av_frm_out->linesize);

            if (sts < 0)
            {
                printf("Error while executing sws_scale");
            }

            int ret = 0;
            do
            {
                if (ret == AVERROR(EAGAIN))
                {
                    av_packet_unref(av_pkt_out);
                    ret = avcodec_receive_packet(av_cntx_out, av_pkt_out);
                    if (ret) break;

                    av_pkt_out->duration = av_rescale_q(1, av_cntx_out->time_base, av_stream->time_base); //???
                    unique_lock<mutex> lk(m_Mutex);
                    av_interleaved_write_frame(ofmt_ctx, av_pkt_out);
                    lk.unlock();
                }
                else if (ret != 0)
                {
                    char str2[] = "";
                    cout << "\nError :" << av_make_error_string(str2, sizeof(str2), ret);
                    return;
                }
                ret = avcodec_send_frame(av_cntx_out, av_frm_out);
            } while (ret);
            av_packet_unref(av_pkt_in);
            av_packet_unref(av_pkt_out);
        }
    }
    int ret = 0;
    avcodec_send_frame(av_cntx_out, NULL);
    do
    {
        av_packet_unref(av_pkt_out);
        ret = avcodec_receive_packet(av_cntx_out, av_pkt_out);
        if (!ret)
        {
            av_interleaved_write_frame(ofmt_ctx, av_pkt_out);
        }
    } while (!ret);

    if (av_write_trailer(ofmt_ctx) < 0)
    {
        cout << "\nerror in writing av trailer";
        exit(1);
    }
}

void RecordVars::MicCaptureThread()
{
    uint8_t** resampledData;
    int ret;

    m_Timer->StartTimer();
    int iFrames = 100;
    int i(0);
    static int64_t pts = 0;
    while (av_read_frame(ifmt_ctx, av_pkt_in) >= 0)
    {
        if (RecordVars::m_bLoopBreaker)
        {
            break;
        }
        av_packet_rescale_ts(av_pkt_out, ifmt_ctx->streams[stream_idx]->time_base, av_cntx_in->time_base);
        if ((ret = avcodec_send_packet(av_cntx_in, av_pkt_in)) < 0)
        {
            cout << "Cannot decode current audio packet " << ret << endl;
            continue;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(av_cntx_in, av_frm_in);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0) {
                cerr << "Error during decoding" << endl;
                exit(1);
            }
            initConvertedSamples(&resampledData, av_cntx_out, av_frm_in->nb_samples);
            swr_convert(resampleContext, resampledData, av_frm_in->nb_samples, (const uint8_t**)av_frm_in->extended_data,
                av_frm_in->nb_samples);

            add_samples_to_fifo(resampledData, av_frm_in->nb_samples);
            av_pkt_out->data = nullptr;
            av_pkt_out->size = 0;
            const int frame_size = FFMAX(av_audio_fifo_size(fifo), av_cntx_out->frame_size);
            av_frm_out = av_frame_alloc();
            if (!av_frm_out)
            {
                cerr << "Cannot allocate an AVPacket for encoded video" << endl;
                exit(1);
            }
            av_frm_out->nb_samples = av_cntx_out->frame_size;
            av_frm_out->ch_layout = av_cntx_out->ch_layout;
            av_frm_out->format = av_cntx_out->sample_fmt;
            av_frm_out->sample_rate = av_cntx_out->sample_rate;
            av_frame_get_buffer(av_frm_out, 0);
            while (av_audio_fifo_size(fifo) >= av_cntx_out->frame_size) {
                ret = av_audio_fifo_read(fifo, (void**)(av_frm_out->data), av_cntx_out->frame_size);
                av_frm_out->pts = pts;
                pts += av_frm_out->nb_samples;
                if (avcodec_send_frame(av_cntx_out, av_frm_out) < 0) {
                    cout << "Cannot encode current audio packet " << endl;
                    exit(1);
                }
                while (ret >= 0) {
                    ret = avcodec_receive_packet(av_cntx_out, av_pkt_out);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    else if (ret < 0) {
                        cerr << "Error during encoding" << endl;
                        exit(1);
                    }
                    unique_lock<mutex> lk(m_Mutex);
                    av_packet_rescale_ts(av_pkt_out, av_cntx_out->time_base, ofmt_ctx->streams[av_stream->index]->time_base);
                    av_pkt_out->stream_index = av_stream->index;
                    if (av_interleaved_write_frame(ofmt_ctx, av_pkt_out) != 0)
                    {
                        cerr << "Error in writing audio frame" << endl;
                    }
                    lk.unlock();
                    av_packet_unref(av_pkt_out);
                }
                ret = 0;
            }
            av_frame_free(&av_frm_out);
            av_packet_unref(av_pkt_out);
        }
    }
    if (*m_MedSettings != AUDIO_FOR_VIDEO)
    {
        if (av_write_trailer(ofmt_ctx) < 0)
        {
            cout << "\nerror in writing av trailer";
            exit(1);
        }
    }

    return;
}

int RecordVars::add_samples_to_fifo(uint8_t** converted_input_samples, const int frame_size)
{
    int error;
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0)
    {
        throw exception("Could not reallocate FIFOn");
    }
    if (av_audio_fifo_write(fifo, (void**)converted_input_samples, frame_size) < frame_size)
    {
        throw exception("Could not write data to FIFOn");
    }
    return 0;
}

int RecordVars::initConvertedSamples(uint8_t*** converted_input_samples, AVCodecContext* output_codec_context, int frame_size)
{
    if (!(*converted_input_samples = (uint8_t**)calloc(output_codec_context->ch_layout.nb_channels,
        sizeof(**converted_input_samples))))
    {
        throw exception("Could not allocate converted input sample pointersn");
    }
    if (av_samples_alloc(*converted_input_samples, nullptr,
        output_codec_context->ch_layout.nb_channels,
        frame_size,
        output_codec_context->sample_fmt, 0) < 0)
    {
        throw exception("Cannot allocate memory for the samples of all channels in one consecutive block for convenience ");
    }
    return 0;
}

void RecordVars::SetMaxTimeofRec(int sec)
{
    m_iMaxTime = sec;
}
void RecordVars::SetCrFValue(string crf)
{
    m_crf = crf.c_str();
}

AVDictionary* OutCodecOptions()
{
	AVDictionary* options(0);
	av_dict_set(&options, "preset", "ultrafast", 0);
	av_dict_set(&options, "rtbufsize", "1500M", 0);
	av_dict_set(&options, "crf", "30", 0);
	return options;
}

int main()
{
	try
	{
		MediaSettings msV = VIDEO_DEFAULT;
		MediaSettings msA = AUDIO_FOR_VIDEO;

		RecordVars recVideo(&msV, "10");
		RecordVars recAudio(&msA);

		recVideo.SetCrFValue("30");

		recVideo.OpenInputMedia("gdigrab", "desktop");
		recVideo.FindMediaStreamIndex(AVMEDIA_TYPE_VIDEO);
		recVideo.SetOutPutFilename(NULL, "out.mp4", NULL);
		recVideo.SetOutputCodecContext(OutCodecOptions());

		recAudio.OpenInputMedia("dshow", "audio=Microphone (2- High Definition Audio Device)");
		recAudio.FindMediaStreamIndex(AVMEDIA_TYPE_AUDIO);
		recAudio.SetOutPutFilename(NULL, "out.mp4", NULL);
		recAudio.SetOutputCodecContext(NULL);

		recVideo.writeFormatHeader();
		recVideo.SetVideoSwsContext();
		recAudio.SetAudioSwrContext();

		recVideo.SetMaxTimeofRec(30);
		recAudio.SetMaxTimeofRec(30);
		RecordVars::VideoAudioThreadDriver(&recVideo, &recAudio);
	}
	catch (exception& ex)
	{
		cout << "Error :" << ex.what();
	}
}

