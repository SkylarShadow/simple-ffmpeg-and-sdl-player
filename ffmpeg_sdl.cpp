// ffmpeg_sdl.cpp: 定义应用程序的入口点。

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

int sfp_refresh_thread(void *opaque) {    /*复习:opaque:不透明指针,指向某种未指定类型的记录或数据结构的指针*/
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
	int y_size;  //yuv分量
	int ret, got_picture;
	struct SwsContext *img_convert_ctx;  //处理图片的结构体,通过sws_getContext()初始化
	char filepath[] = "D:/project/program/vs2017project/ffmpeg_sdl/output.h265";

	//SDL部分——结构体定义
	int screen_w = 0, screen_h = 0;
	SDL_Window *screen;   //定义了一个sdl2的窗口
	SDL_Renderer* sdlRenderer; //基于窗口创建渲染器
	SDL_Texture* sdlTexture; //创建纹理
	SDL_Rect sdlRect;	//矩形

	SDL_Thread *video_tid;
	SDL_Event event;



	avformat_network_init();//加载socket库以及网络加密协议相关的库，为后续使用网络相关提供支持 
	pFormatCtx = avformat_alloc_context();//为 AVFormatContext 结构体分配动态内存，然后调用 avformat_get_context_defaults 函数获取该 AVFormatContext 的默认值

	/*
	 输入输出结构体AVIOContext的初始化；
	 输入数据的协议（例如RTMP，或者file）的识别（通过一套评分机制）:1判断文件名的后缀 2读取文件头的数据进行比对，使用获得最高分的文件协议对应的URLProtocol，通过函数指针的方式，与FFMPEG链接
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
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { //判断编码数据是否为视频
			videoindex = i;
			break;
		}

	if (videoindex == -1) {
		cout << "无法获取视频流." << endl;
		return -1;
	}


	pCodecPar = pFormatCtx->streams[videoindex]->codecpar;  
	pCodec = (AVCodec*)avcodec_find_decoder(pCodecPar->codec_id);  //通过id找解码器
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

	/*
	uint8_t *data：解码后原始数据;yuv中,例:data[0]存放的是y分量数据的首地址，data[1]存放的是u的首地址，data[2]存放的是v的首地址
	int linesize[AV_NUM_DATA_POINTERS]：data(解码后原始数据)中“一行”数据的大小,未必等于图像的宽，一般大于图像的宽(为了对齐补0)  例:linesize[0]就表示y分量每一行的长度
	linesize方便找分量的首地址 例:(data[0]+linesize[0])即(data[0]+一行的长度)
	在yuv420p中，若图像分辨率为w*h，y分量有h行linesize[0]列
	*/
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

		函数完成如下操作
		1.  对SwsContext中的各种变量进行赋值
		2.  通过sws_rgb2rgb_init()初始化RGB转RGB（或者YUV转YUV）的函数（注意不包含RGB与YUV相互转换的函数）
		3.  通过判断输入输出图像的宽高来判断图像是否需要拉伸。如果图像需要拉伸，那么unscaled变量会被标记为1
		4.  通过sws_setColorspaceDetails()初始化颜色空间。
		5.  一些输入参数的检测。例如：如果没有设置图像拉伸方法的话，默认设置为SWS_BICUBIC；如果输入和输出图像的宽高小于等于0的话，返回错误信息
		6.  初始化Filter。这一步根据拉伸方法的不同，初始化不同的Filter
		7.  如果flags中设置了“打印信息”选项SWS_PRINT_INFO，则输出信息
		8.  如果不需要拉伸的话，调用ff_get_unscaled_swscale()将特定的像素转换函数的指针赋值给SwsContext中的swscale指针
		9.  如果需要拉伸的话，调用ff_getSwsFunc()将通用的swscale()赋值给SwsContext中的swscale指针
	*/

	img_convert_ctx = sws_getContext(pCodecPar->width, pCodecPar->height, pCodecCtx->pix_fmt,
		pCodecPar->width, pCodecPar->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


#if OUTPUT_YUV420P 

	FILE *fp_yuv;
	fp_yuv = fopen("output.yuv", "wb+");

#endif  

	/*SDL_Init()可选参数：
		SDL_INIT_TIMER：定时器
		SDL_INIT_AUDIO：音频
		SDL_INIT_VIDEO：视频
		SDL_INIT_JOYSTICK：摇杆
		SDL_INIT_HAPTIC：触摸屏
		SDL_INIT_GAMECONTROLLER：游戏控制器
		SDL_INIT_EVENTS：事件
		SDL_INIT_NOPARACHUTE：不捕获关键信号（这个不理解）
		SDL_INIT_EVERYTHING：包含上述所有选项 
	*/
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

	/*
	    SDL_Renderer * SDLCALL SDL_CreateRenderer(SDL_Window * window,int index, Uint32 flags);
		window ： 渲染的目标窗口。

		index ：初始化的渲染设备的索引。设置“-1”则初始化默认的渲染设备。

		flags ：
		SDL_RENDERER_SOFTWARE ：使用软件渲染
		SDL_RENDERER_ACCELERATED ：使用硬件加速
		SDL_RENDERER_PRESENTVSYNC：和显示器的刷新率同步
		SDL_RENDERER_TARGETTEXTURE
	*/
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	/*
	SDL_Texture * SDLCALL SDL_CreateTexture(SDL_Renderer * renderer,        //目标渲染器
                                                        Uint32 format,		//纹理格式
                                                        int access, 		//access ：SDL_TEXTUREACCESS_STATIC：变化极少;SDL_TEXTUREACCESS_STREAMING：变化频繁;SDL_TEXTUREACCESS_TARGET()
                                                       int w, int h);
	*/
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
			got_picture = avcodec_send_packet(pCodecCtx, packet);  //发送数据到ffmepg，将数据放到解码队列
			ret = avcodec_receive_frame(pCodecCtx, pFrame);			// 从解码成功的队列中取出1个frame    0：成功
			if (ret < 0) {
				cout<<"解码错误"<<endl;
				return -1;
			}
			if (got_picture==0) {
				/*struct SwsContext *sws_getContext(
									int srcW, // 输入图像的宽度 
									int srcH, // 输入图像的宽度 
									enum AVPixelFormat srcFormat, // 输入图像的像素格式 
									int dstW, // 输出图像的宽度 
									int dstH, // 输出图像的高度 
									enum AVPixelFormat dstFormat, // 输出图像的像素格式 
									int flags,// 选择缩放算法(只有当输入输出图像大小不同时有效),一般选择SWS_FAST_BILINEAR
									SwsFilter *srcFilter, // 输入图像的滤波器信息, 若不需要传NULL 
									SwsFilter *dstFilter, // 输出图像的滤波器信息, 若不需要传NULL 
									const double *param );
				用于视频像素格式和分辨率的转换
									*/
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

#if OUTPUT_YUV420P
	int y_size = pCodecCtx->width*pCodecCtx->height;
	fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
	fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
	fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
#endif
				//SDL---------------------------


	/*
	int SDLCALL SDL_UpdateTexture(SDL_Texture * texture,			//texture：目标纹理
                                  const SDL_Rect * rect,			//rect：更新像素的矩形区域。设置为NULL的时候更新整个区域
                                  const void *pixels,				//pixels：像素数据
									int pitch);						//pitch：一行像素数据的字节数
	*/
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);  
				SDL_RenderClear(sdlRenderer);
				//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);  //把纹理复制给渲染器
				SDL_RenderPresent(sdlRenderer);//显示
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