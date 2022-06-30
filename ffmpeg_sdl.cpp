// ffmpeg_sdl.cpp: 定义应用程序的入口点。
//
//读取文件有问题，看一下结构体
#define OUTPUT_YUV420P 0
#define __STDC_CONSTANT_MACROS
#include<iostream>
#include<fstream>
#include<stdlib.h>
#include "SDL.h"


extern"C"
{
	#include<libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/imgutils.h>

}
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"SDL2.lib")
#pragma comment(lib,"SDL2main.lib")
#pragma comment(lib,"SDL2test.lib")
  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)


//刷新事件
int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void *opaque) {
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(40);
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}


using namespace std;
int main(int argc, char *argv[]) {
	
	AVFormatContext *pFormatCtx;  //统领全局的基本结构体。主要用于处理封装格式（FLV/MKV/RMVB等）
	int i, videoindex;
	AVCodecContext *pCodecCtx;
	AVCodecParameters *pCodecPar;   //AVCodecContext 音视频流对应的结构体，用于音视频编解码
	AVCodec	*pCodec;		//存储编解码器信息的结构体
	AVFrame	*pFrame, *pFrameYUV;//包含码流参数较多的结构体，是解码后的数据
	unsigned char *out_buffer;
	AVPacket *packet;  //存储压缩数据（视频对应H.264等码流数据，音频对应AAC/MP3等码流数据），解码前的数据
	int y_size;
	int ret, got_picture;
	struct SwsContext *img_convert_ctx;  //貌似无法看到内部结构，通过sws_getContext()初始化
	char filepath[] = "D:/project/program/vs2017project/ffmpeg_sdl/output.h265";

	//SDL部分
	int screen_w = 0, screen_h = 0;
	SDL_Window *screen;   //定义了一个sdl2的窗口
	SDL_Renderer* sdlRenderer; //渲染器
	SDL_Texture* sdlTexture; //纹理
	SDL_Rect sdlRect;	//矩形

	SDL_Thread *video_tid;
	SDL_Event event;



	avformat_network_init();//加载socket库以及网络加密协议相关的库，为后续使用网络相关提供支持 
	pFormatCtx = avformat_alloc_context();//为 AVFormatContext 结构体分配动态内存，然后调用 avformat_get_context_defaults 函数获取该 AVFormatContext 的默认值

	/*
	 输入输出结构体AVIOContext的初始化；
	 输入数据的协议（例如RTMP，或者file）的识别（通过一套评分机制）:1判断文件名的后缀 2读取文件头的数据进行比对，使用获得最高分的文件协议对应的URLProtocol，通过函数指针的方式，与FFMPEG连接（非专业用词）；
	 调用该URLProtocol的函数进行open,read等操作
	 avformat_open_input(AVFormatContext **ps, const char *filename, AVInputFormat *fmt, AVDictionary **options);
	 ps：函数调用成功之后处理过的AVFormatContext结构体
	 file：打开的视音频流的地址
	 fmt：强制指定AVFormatContext中AVInputFormat，一般情况下设置为NULL，这样FFmpeg可以自动检测AVInputFormat
	 dictionay：附加选项，一般情况下设置为NULL
	*/
	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) { 
		cout<<"无法打开视频流."<<endl;
		return -1;
	}

	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		cout<<"无法获取视频流信息"<<endl;
		return -1;
	}

	videoindex = -1;

	for (i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { 
			videoindex = i;
			break;
		}
	if (videoindex == -1) {
		cout << "无法获取视频流." << endl;
		return -1;
	}


	pCodecPar = pFormatCtx->streams[videoindex]->codecpar;   //stream的类型是一个指针的指针，
	pCodec = (AVCodec*)avcodec_find_decoder(pCodecPar->codec_id);
	pCodecCtx = avcodec_alloc_context3(pCodec);
	avcodec_parameters_to_context(pCodecCtx, pCodecPar);


	if (pCodec == NULL) {
		cout << "找不到编码器" << endl;
		return -1;
	}

	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		cout<<"不能打开编码器.\n"<<endl;
		return -1;
	}

	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	cout<<"---------------- 文件信息 ---------------"<<endl;
	/*
		void av_dump_format(AVFormatContext *ic,int index,const char *url,int is_output);
		//打印关于输入或输出格式的详细信息，例如持续时间，比特率，流，容器，程序，元数据，边数据，编解码器和时基。
		 is_output  0，打印输入流；1，打印输出流
	*/
	av_dump_format(pFormatCtx, 0, filepath, 0);
	cout<<"-------------------------------------------------"<<endl;

	/*
	struct SwsContext *sws_getContext(int srcW, int srcH, enum AVPixelFormat srcFormat,  
                                  int dstW, int dstH, enum AVPixelFormat dstFormat,  
                                  int flags, SwsFilter *srcFilter,  
                                  SwsFilter *dstFilter, const double *param); 

		srcW：源图像的宽
		srcH：源图像的高
		srcFormat：源图像的像素格式
		dstW：目标图像的宽
		dstH：目标图像的高
		dstFormat：目标图像的像素格式
		flags：设定图像拉伸使用的算法
	*/

	img_convert_ctx = sws_getContext(pCodecPar->width, pCodecPar->height, pCodecCtx->pix_fmt,
		pCodecPar->width, pCodecPar->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


#if OUTPUT_YUV420P 

	FILE *fp_yuv;
	fp_yuv = fopen("output.yuv", "wb+");

#endif  

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		cout<<"初始化sdl失败 - "<< SDL_GetError()<<endl;
		return -1;
	}

	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;

	screen = SDL_CreateWindow("ffmpeg player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h,
		SDL_WINDOW_OPENGL);

	if (!screen) {
		cout<<"SDL创建窗口失败 - 原因:"<< SDL_GetError()<<endl;
		return -1;
	}

	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	packet = av_packet_alloc();


	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

	while(1) {
		//Wait
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			while (1) {
				if (av_read_frame(pFormatCtx, packet) < 0)
					thread_exit = 1;

				if (packet->stream_index == videoindex)
					break;
			}
			got_picture = avcodec_send_packet(pCodecCtx, packet);
			ret = avcodec_receive_frame(pCodecCtx, pFrame);
			if (ret < 0) {
				cout<<"解码错误"<<endl;
				return -1;
			}
			if (got_picture==0) {
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

#if OUTPUT_YUV420P
	int y_size = pCodecCtx->width*pCodecCtx->height;
	fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
	fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
	fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
#endif
				//SDL---------------------------
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
				//SDL End-----------------------
			}
			av_packet_unref(packet);
		}
		else if (event.type == SDL_KEYDOWN) {
			//Pause
			if (event.key.keysym.sym == SDLK_SPACE)
				thread_pause = !thread_pause;
		}
		else if (event.type == SDL_QUIT) {
			thread_exit = 1;
		}
		else if (event.type == SFM_BREAK_EVENT) {
			break;
		}

	}
	
#if OUTPUT_YUV420P 
		fclose(fp_yuv);
#endif 
		
	sws_freeContext(img_convert_ctx);
	SDL_Quit();

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	system("pause");
	return 0;
}