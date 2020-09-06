#include <iostream>
#include <cstdlib>
#include <queue>
#include <thread>
#include <string>
#include"video1.h"
#define __STDC_CONSTANT_MACROS
#define SDL_MAIN_HANDLED
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_main.h>
#include <al.h>
#include <alc.h>
}
using namespace std;
#pragma comment(lib ,"SDL2.lib")
#pragma comment(lib ,"SDL2main.lib")

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(int timeInterval, bool& faster, bool& slower) {
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		if (faster) {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval) / 2);
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
		}
		if (slower) {
			SDL_Delay(20);
		}
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}
//初始化openal
void setopenal(ALuint source)
{
	ALfloat SourceP[] = { 0.0, 0.0, 0.0 };
	ALfloat SourceV[] = { 0.0, 0.0, 0.0 };
	ALfloat ListenerPos[] = { 0.0, 0, 0 };
	ALfloat ListenerVel[] = { 0.0, 0.0, 0.0 };
	ALfloat ListenerOri[] = { 0.0, 0.0, -1.0,  0.0, 1.0, 0.0 };
	alSourcef(source, AL_PITCH, 1.0);
	alSourcef(source, AL_GAIN, 1.0);
	alSourcefv(source, AL_POSITION, SourceP);
	alSourcefv(source, AL_VELOCITY, SourceV);
	alSourcef(source, AL_REFERENCE_DISTANCE, 50.0f);
	alSourcei(source, AL_LOOPING, AL_FALSE);
	alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
	alListener3f(AL_POSITION, 0, 0, 0);

}


std::queue<PTFRAME> queueData; //保存解码后数据
ALuint m_source;
double audio_pts;
int64_t audio_timestamp;

int SoundCallback(ALuint & bufferID) {
	if (queueData.empty()) return -1;
	PTFRAME frame = queueData.front();
	queueData.pop();
	if (frame == nullptr)
		return -1;
	//把数据写入buffer
	alBufferData(bufferID, AL_FORMAT_STEREO16, frame->data, frame->size, frame->samplerate);
	//将buffer放回缓冲区
	alSourceQueueBuffers(m_source, 1, &bufferID);
	audio_pts = frame->audio_clock;

	//释放数据
	if (frame) {
		av_free(frame->data);
		delete frame;
	}
	return 0;
}




//sdl渲染画面
int sdlplayer(string filePath) {
	AVFormatContext* pFormatCtx;
	int				i, videoindex;
	AVCodecContext* pCodecCtx;
	AVCodec* pCodec;
	AVFrame* pFrame, * pFrameYUV;
	unsigned char* out_buffer;
	AVPacket* packet;
	int ret, got_pic;

	//------------SDL----------------
	int screen_w, screen_h;
	
	SDL_Thread* video_tid;
	SDL_Event event;

	struct SwsContext* img_convert_ctx;
	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();

	if (avformat_open_input(&pFormatCtx, filePath.c_str(), NULL, NULL) != 0) {
		cout<<"Couldn't open input stream"<<endl;
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		cout<<"Couldn't find stream information"<<endl;
		return -1;
	}
	videoindex = -1;
	//video
	for (i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	if (videoindex == -1) {
		printf("Didn't find a video stream.\n");
		return -1;
	}
	pCodecCtx = pFormatCtx->streams[videoindex]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);


	av_dump_format(pFormatCtx, 0, filePath.c_str(), 0);	

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

   //放sdl
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}                          
	SDL_Window* screen;
	screen_w = pCodecCtx->width; 
	screen_h = pCodecCtx->height;
	screen = SDL_CreateWindow("goudan", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);

	if (!screen) {
		cout<<"SDL: could not create window - exiting:%s"<<endl;
		return -1;
	}
	SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	SDL_Texture*  sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	Uint32 pixformat = 0;
	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	pixformat = SDL_PIXELFORMAT_IYUV;
	
	//不再读文件，直接让yuv显示
	SDL_Rect sdlRect;


	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	double frameRate = (double)pCodecCtx->framerate.num / pCodecCtx->framerate.den;
	bool faster = false;
	bool slower = false;
	//视频更新线程
	std::thread refreshThread(sfp_refresh_thread, (int)(frameRate), std::ref(faster), std::ref(slower));

	double video_pts = 0;
	double delay = 0;
	while (audio_pts >= 0) {
		//Wait
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			while (1) {
				if (av_read_frame(pFormatCtx, packet) < 0)
					thread_exit = 1;

				if (packet->stream_index == videoindex)
					break;
			}
			ret = avcodec_send_packet(pCodecCtx, packet);
			got_pic = avcodec_receive_frame(pCodecCtx, pFrame);
			if (ret < 0) {
				cout << "Decode Error." << endl;
				return -1;
			}
			if (!got_pic) {
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0,
					pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
			}//展示视频画面
			while (1) {
				SDL_WaitEvent(&event);
				if (event.type == SFM_REFRESH_EVENT) {
					if (queueData.empty()) {   //队空情况
						sws_freeContext(img_convert_ctx);
						SDL_Quit();
						av_frame_free(&pFrameYUV);
						av_frame_free(&pFrame);
						avcodec_close(pCodecCtx);
						avformat_close_input(&pFormatCtx);
					}
					if (true) {//控制视频加载快慢
						video_pts = (double)pFrame->pts * av_q2d(pFormatCtx->streams[videoindex]->time_base); //获得视频时间戳
						delay = audio_pts - video_pts;
						if (delay > 0.03) {
							faster = true;
						}
						else if (delay < -0.03) {
							faster = false;
							slower = true;
						}
						else {
							faster = false;
							slower = false;
						}

					}
					SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
					sdlRect.x = 0;
					sdlRect.y = 0;
					sdlRect.w = screen_w;
					sdlRect.h = screen_h;
					SDL_RenderClear(sdlRenderer);
					SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
					SDL_RenderPresent(sdlRenderer);
					av_free_packet(packet);
					break;
				}
				else if (event.type == SDL_QUIT) {
					thread_exit = 1;
				}
				else if (event.type == SFM_BREAK_EVENT) {
					break;
				}
			}//还可以再添加其他事件

		}


	}
	sws_freeContext(img_convert_ctx);
	SDL_Quit();
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
}

int main(int agrc,char* argv[]) {

	string filepath = argv[1];  //输入文件mp4
	//ffmepg相关变量
	AVFormatContext* pFormatCtx; ////AVFormatContext主要存储视音频封装格式中包含的信息  
	unsigned             i;
	int             audioindex;//音频流所在序号
	AVCodecContext* pCodecCtx_audio;//AVCodecContext，存储该音频流使用解码方式的相关数据
	AVCodec* pCodec_audio;//音频解码器 
	AVFrame* pFrame_audio;

	SwrContext* swrCtx;
	double audio_clock = 0;
	av_register_all();	//初始化libformat库和注册编解码器
	avformat_network_init();
	avcodec_register_all();
	pFormatCtx = avformat_alloc_context();

	//audio
	if (avformat_open_input(&pFormatCtx, filepath.c_str(), NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	//获取流信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	//获取各个媒体流的编码器信息，找到对应的type所在的pFormatCtx->streams的索引位置，初始化编码器。播放音频时type是AUDIO
	audioindex = -1;
	//找到音频流的序号
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioindex = i;
			break;
		}
	if (audioindex == -1) {
		printf("Didn't find a video stream.\n");
		return -1;
	}
	//获取解码器
	pCodec_audio = avcodec_find_decoder(pFormatCtx->streams[audioindex]->codecpar->codec_id);
	if (pCodec_audio == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	pCodecCtx_audio = avcodec_alloc_context3(pCodec_audio);
	avcodec_parameters_to_context(pCodecCtx_audio, pFormatCtx->streams[audioindex]->codecpar);
	pCodecCtx_audio->pkt_timebase = pFormatCtx->streams[audioindex]->time_base;
	//打开解码器
	if (avcodec_open2(pCodecCtx_audio, pCodec_audio, NULL) < 0) {
		printf("Couldn't open codec.\n");
		return -1;
	}

	//内存分配
	AVPacket* packet;
	packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	pFrame_audio = av_frame_alloc();
	

	//设置输出的音频参数
	int out_nb_samples = 1024;//单个通道样本个数
	int out_sample_rate = 44100;//输出时采样率,CD一般为44100HZ
	int in_sample_rate = pCodecCtx_audio->sample_rate; //输入采样率
	enum AVSampleFormat in_sample_fmt = pCodecCtx_audio->sample_fmt;  //输入的采样格式  
	enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16; //输出采样格式16bit PCM  
	uint64_t in_ch_layout = pCodecCtx_audio->channel_layout; //输入的声道布局   
	uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO; //输出的声道布局（立体声）
	int out_channels = av_get_channel_layout_nb_channels(out_ch_layout);//根据通道布局类型获取通道数
    //根据通道数、样本个数、采样格式分配内存
	int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
	uint8_t* out_buffer_audio;
	
	SDL_AudioSpec wanted_spec;
	wanted_spec.freq = out_sample_rate;//采样率,即播放速度
	wanted_spec.format = AUDIO_S16SYS;//音频数据格式。在“S16SYS”中的S 表示有符号的signed，
									  //16 表示每个样本是16 位长的，SYS 表示大小头的顺序是与使用的系统相同的。
									  //这些格式是由 avcodec_decode_audio 为我们给出来的输入音频的格式
	wanted_spec.channels = out_channels;//声道数
	wanted_spec.silence = 0; //静音值，因为数据是有符号数，所以0表示静音
	wanted_spec.samples = out_nb_samples;//这是当我们想要更多声音的时候，我们想让SDL 给出来的声音缓冲
										 //区的尺寸。一个比较合适的值在512 到8192 之间；ffplay 使用1024。
	wanted_spec.userdata = pCodecCtx_audio;//用户数据，它将提供给回调函数使用，这里即编解码上下文
	//打开音频
	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		printf("can't open audio.\n");
		return -1;
	}
    //获取声道布局
	pCodecCtx_audio->channel_layout = av_get_default_channel_layout(pCodecCtx_audio->channels);


	//swr
	swrCtx = swr_alloc();
	swrCtx = swr_alloc_set_opts(swrCtx,
		out_ch_layout, out_sample_fmt, out_sample_rate,
		in_ch_layout, in_sample_fmt, in_sample_rate,
		0, NULL); //设置参数
	swr_init(swrCtx); //初始化

	

	int ret;
	while (av_read_frame(pFormatCtx, packet) >= 0) {//读取下一帧数据
		if (packet->stream_index == audioindex) {
			ret = avcodec_send_packet(pCodecCtx_audio, packet);
			if (ret < 0) {
				cout << "avcodec_send_packet：" << ret << endl;
				continue;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(pCodecCtx_audio, pFrame_audio);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				else if (ret < 0) {
					cout << "avcodec_receive_frame：" << AVERROR(ret) << endl;
					return -1;
				}

				if (ret >= 0) {  
					out_buffer_audio = (uint8_t*)av_malloc(MAX_AUDIO_FARME_SIZE*2);//*2是保证输出缓存大于输入数据大小
					//重采样
					swr_convert(swrCtx, &out_buffer_audio, MAX_AUDIO_FARME_SIZE, (const uint8_t * *)pFrame_audio->data, pFrame_audio->nb_samples);
					
					out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, pFrame_audio->nb_samples, out_sample_fmt, 1);
					PTFRAME frame = new TFRAME;
					frame->data = out_buffer_audio;
					frame->size = out_buffer_size;
					frame->samplerate = out_sample_rate;
					audio_clock = av_q2d(pCodecCtx_audio->time_base) * pFrame_audio->pts;
					frame->audio_clock = audio_clock;
					
					queueData.push(frame);  //解码后数据存入队列
				}
			}
		}
		av_packet_unref(packet);
	}


	ALCdevice* pDevice;
	ALCcontext* pContext;

	pDevice = alcOpenDevice(NULL);
	pContext = alcCreateContext(pDevice, NULL);
	alcMakeContextCurrent(pContext);

	if (alcGetError(pDevice) != ALC_NO_ERROR)
		return AL_FALSE;

	ALuint m_buffers[NUMBUFFERS];
	alGenSources(1, &m_source);
	if (alGetError() != AL_NO_ERROR) {
		cout << "Error generating audio source." << endl;
		return -1;
	}
	setopenal(m_source);

	alGenBuffers(NUMBUFFERS, m_buffers); //创建缓冲区

	ALint processed1 = 0;
	alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed1);

	std::thread sdlplay{ sdlplayer, filepath };
	sdlplay.detach();

	for (int i = 0; i < NUMBUFFERS; i++) {
		SoundCallback(m_buffers[i]);
	}
	alSourcePlay(m_source);
	while (!queueData.empty()) {  //队列为空后停止播放
		ALint processed = 0;
		alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
		while (processed > 0) {
			ALuint bufferID = 0;
			alSourceUnqueueBuffers(m_source, 1, &bufferID);
			SoundCallback(bufferID);
			processed--;
		}
		int state;
		alGetSourcei(m_source, AL_SOURCE_STATE, &state);
		if (state == AL_STOPPED || state == AL_INITIAL) {
			alSourcePlay(m_source);
		}
		
	}

	alSourceStop(m_source);
	alSourcei(m_source, AL_BUFFER, 0);
	alDeleteBuffers(NUMBUFFERS, m_buffers);
	alDeleteSources(1, &m_source);

	

	av_frame_free(&pFrame_audio);
	swr_free(&swrCtx);


	ALCcontext* pCurContext = alcGetCurrentContext();
	ALCdevice* pCurDevice = alcGetContextsDevice(pCurContext);

	alcMakeContextCurrent(NULL);
	alcDestroyContext(pCurContext);
	alcCloseDevice(pCurDevice);


	return 0;
}

