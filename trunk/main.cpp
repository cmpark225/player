
extern "C"
{
#include "libavcodec\avcodec.h"
#include "libavformat\avformat.h"
#include "libswscale\swscale.h"
};

#include "SDL.h"
#include "SDL_thread.h"

#include "FFmpegPlayer.h"

#include <Windows.h>
#include <Shlobj.h>
#include <stringapiset.h>
#include <assert.h>

#pragma (lib, "Shell32")
#pragma (lib, "Kernel32")

typedef struct PacketQueue
{
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;

} PacketQueue;


PacketQueue audioq;

BOOL fnBrowseFolder( char *szFilePath)
{
	OPENFILENAME ofn;

	TCHAR wPath[MAX_PATH] = {};

	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = wPath;
	ofn.nMaxFile = sizeof(wPath);
	ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags =  OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileName(&ofn))
	{
		WideCharToMultiByte(CP_ACP, 0, wPath, lstrlenW(wPath), szFilePath, MAX_PATH,NULL, NULL);
		return TRUE;
	}
	else
		return FALSE;
}


SDL_Rect rect;
SDL_Overlay *bmp;
void draw()
{
	SDL_DisplayYUVOverlay(bmp, &rect);
}


int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{
	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	static AVFrame frame;

	int len1, data_size = 0;

	for (;;) {
		while (audio_pkt_size > 0) {
			int got_frame = 0;
			len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
			if (len1 < 0) {
				/* if error, skip frame */
				audio_pkt_size = 0;
				break;
			}
			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			data_size = 0;
			if (got_frame) {
				data_size = av_samples_get_buffer_size(NULL,
					aCodecCtx->channels,
					frame.nb_samples,
					aCodecCtx->sample_fmt,
					1);
				assert(data_size <= buf_size);
				memcpy(audio_buf, frame.data[0], data_size);
			}
			if (data_size <= 0) {
				/* No data yet, get more frames */
				continue;
			}
			/* We have data, return it and come back for more later */
			return data_size;
		}
		if (pkt.data)
			av_free_packet(&pkt);
		/*
				if (quit) {
				return -1;
				}*/

		if (packetQueueGet(&audioq, &pkt, 1) < 0) {
			return -1;
		}
		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}
}
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 

void audio_callback(void *userdata, Uint8 *stream, int len)
{

	AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int len1, audio_size;

	static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	while (len > 0) {
		if (audio_buf_index >= audio_buf_size) {
			/* We have already sent all our data; get more */
			audio_size = audio_decode_frame(aCodecCtx, audio_buf,
				sizeof(audio_buf));
			if (audio_size < 0) {
				/* If error, output silence */
				audio_buf_size = 1024;
				memset(audio_buf, 0, audio_buf_size);
			}
			else {
				audio_buf_size = audio_size;
			}
			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;
		memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}
}
void packetQueueInit(PacketQueue *q)
{
	memset(q, 0, sizeof(PacketQueue));

	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packetQueuePut(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pkt1;
	if (av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 =(AVPacketList*) av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;


	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

int quit = 0;

static int packetQueueGet(PacketQueue *q, AVPacket *pkt, int block)
{
	AVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {

		if (quit) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

int main(int argc, char *argv[])
{

	// SDL 초기화
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("could not initailize sdl %s", SDL_GetError());
		return -1;
	}

	char Path[MAX_PATH] = {};
	if (!fnBrowseFolder(Path))
		return -1;

	CFFmpegPlayer player;

	if (player.fileOpen(Path) < 0)
		return -1;

	if (player.openCodec() < 0)
		return -1;


	// audio 출력
	SDL_AudioSpec wanted_spec, spec = {};

	wanted_spec.freq = player.getAudioCodecContext()->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = player.getAudioCodecContext()->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = 1024;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = player.getAudioCodecContext();


	if (SDL_OpenAudio(&wanted_spec, NULL) < 0)
	{
		printf("sdl_openAudio : %s\n", SDL_GetError());
		return -1;
	}
	packetQueueInit(&audioq);
	SDL_PauseAudio(0);

	// 화면 사용을 위한 Surface 생성
	SDL_Surface *screen = SDL_SetVideoMode(player.getWidth(), player.getHeight(), 0, 0); //pCodecCtx->width,pCodecCtx->height, 0, 0); 
	//SDL_Overlay *
	bmp = SDL_CreateYUVOverlay(player.getWidth(), player.getHeight(), SDL_YV12_OVERLAY, screen); //pCodecCtx->width, pCodecCtx->height, SDL_YV12_OVERLAY, screen);

	
	rect.x = 0;
	rect.y = 0;
	rect.w = player.getWidth(); //pCodecCtx->width;
	rect.h = player.getHeight(); //pCodecCtx->height;

	SDL_LockYUVOverlay(bmp);

	AVPicture pict;
	pict.data[0] = bmp->pixels[0];
	pict.data[1] = bmp->pixels[2];
	pict.data[2] = bmp->pixels[1];

	pict.linesize[0] = bmp->pitches[0];
	pict.linesize[1] = bmp->pitches[2];
	pict.linesize[2] = bmp->pitches[1];
	SDL_UnlockYUVOverlay(bmp);


	player.play(draw, pict.data, pict.linesize);


	return 0;
}


void getStreamType(AVFormatContext* pFmtCtx)
{

	for (int i = 0; i < pFmtCtx->nb_streams; i++)
	{
		printf("Stream %d is ", i);
		switch (pFmtCtx->streams[i]->codec->codec_type)
		{
		case AVMEDIA_TYPE_VIDEO:
			printf("video\n");
			break;
		case AVMEDIA_TYPE_AUDIO:
			printf("audio\n");
			break;
		case AVMEDIA_TYPE_DATA:          ///< Opaque data information usually continuous
			printf("data\n");
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			printf("subtitle\n");
			break;
		case AVMEDIA_TYPE_ATTACHMENT:    ///< Opaque data information usually sparse
			printf("attachment\n");
			break;
		case AVMEDIA_TYPE_NB:
			printf("nb\n");
			break;
		default:
			break;
		}

	}
}
//
//
//int main()
//{
//	const char *szFilePath = "C:\\Users\\cmpark\\Desktop\\sample\\test.mp4";
//
//	av_register_all();
//
//	avformat_network_init();
//
//	int ret;
//
//	AVFormatContext *pFmtCtx = NULL;
//
//
//	ret = avformat_open_input(&pFmtCtx, szFilePath, NULL, NULL);
//
//	if (ret != 0)
//	{
//		av_log(NULL, AV_LOG_ERROR, "File[%s] Open Fail(ret : %d)\n", ret);
//		return -1;
//	}
//
//
//	av_log(NULL, AV_LOG_INFO, "File[%s] Open Success\n", szFilePath);
//
//	//ret = avformat_find_stream_info(pFmtCtx, NULL);
//	//if (ret < 0)
//	//{
//	//	av_log(NULL, AV_LOG_ERROR, "Fail to get Stream Infomation\n");
//	//	return -1;
//	//}
//
//	//av_log(NULL, AV_LOG_INFO, "Get Stream Infomation Success\n");
//	//getStreamType(pFmtCtx);
//
//
//	int nVSI = -1;
//	int nASI = -1;
//
//	for (int i = 0; i < pFmtCtx->nb_streams; i++)
//	{
//		if (pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//			nVSI = nVSI < 0 ? i : nVSI;
//		else if (pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
//			nASI = nASI < 0 ? i : nASI;
//
//	}
//
//
//	//nVSI = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
//	//nVSI = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
//
//	// 디코더 검색 및 초기화
//
//	AVCodec *pVideoCodec = avcodec_find_decoder(pFmtCtx->streams[nVSI]->codec->codec_id);
//
//
//	if (pVideoCodec == NULL)
//	{
//		av_log(NULL, AV_LOG_ERROR, "No Decoder was Found\n");
//		return -1;
//	}
//
//
//	if (avcodec_open2(pFmtCtx->streams[nVSI]->codec, pVideoCodec, NULL) < 0)
//	{
//		av_log(NULL, AV_LOG_ERROR, "Fail to Initialize Decoder\n");
//		return -1;
//	}
//
//	AVCodecContext *pVCtx = pFmtCtx->streams[nVSI]->codec;
//	AVCodecContext *pActx = pFmtCtx->streams[nASI]->codec;
//
//	AVPacket packet;
//	AVFrame *pVFrame = av_frame_alloc();
//	AVFrame *pAFrame = av_frame_alloc();
//	AVFrame* pFrameRGB = av_frame_alloc();
//
//	int bGotPicture = 0;
//	int bGotSound = 0;
//
//	cv::namedWindow("playVideo", CV_WINDOW_AUTOSIZE);
//
//
//	
//	uint8_t* buffer = NULL;
//	int numBytes;
//	//Determine required buffer size and allocate buffer
//	numBytes = avpicture_get_size(PIX_FMT_RGB32, pVCtx->width, pVCtx->height);
//	buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
//
//	//assign appropriate parts of buffer to image planes in FrameRGB
//	//Note the pFrameRGB is an AVFrame, but AVFrame is z superset
//	// of AVPicture
//
//
//	while (av_read_frame(pFmtCtx, &packet) >= 0)
//	{
//		if (packet.stream_index == nVSI)
//		{
//			avcodec_decode_video2(pVCtx, pVFrame, &bGotPicture, &packet);
//			if (bGotPicture)
//			{
//
//				avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB32, pVCtx->width, pVCtx->height);
//
//
//				cv::Mat frame(pVCtx->width, pVCtx->height, CV_8UC3, (void *)pVFrame->data);
//				cv::imshow("playVideo", frame);
//
//
//			}
//		}
//		else if (packet.stream_index == nASI)
//		{
//			avcodec_decode_audio4(pActx, pAFrame, &bGotSound, &packet);
//			if (bGotSound)
//			{
//
//			}
//		}
//
//		av_free_packet(&packet);
//	}
//
//
//
//	av_free(pVFrame);
//	av_free(pAFrame);
//
//	avformat_close_input(&pFmtCtx);
//
//	avformat_network_deinit();
//}
//
