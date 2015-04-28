#include "FFmpegPlayer.h"


CFFmpegPlayer::CFFmpegPlayer()
{
	av_register_all();

}


CFFmpegPlayer::~CFFmpegPlayer()
{
	avformat_close_input(&pFormatCtx);
	avcodec_close(pCodecCtx);

}

int CFFmpegPlayer::fileOpen(const char *fileName)
{
	if (avformat_open_input(&pFormatCtx, fileName, NULL, 0) != 0)
	{
		printf("can not open file\n");
		return -1;
	}

	// 스트림 정보를 찾는다
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return -1;

	// dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, fileName, 0);

}

int CFFmpegPlayer::openCodec()
{
	if(openVideoCodec() < 0)
		return -1;

	if (openAudioCodec() < 0)
		return -1;

	return 1;
}

int CFFmpegPlayer::openVideoCodec()
{
	idxVideoStream = findStreamIndex(AVMEDIA_TYPE_VIDEO);
	if (idxVideoStream == -1) //can't find video stream 
		return -1;

	//get a pointer to the codec context for the video  stream
	//코덱에 관한 스트림 정보를 codec context라고 부릅니다.이것은 코덱에 관한 모든 정보를 갖고 있습니다.
	AVCodecContext* pCodecCtxOrig = NULL;
	pCodecCtxOrig = pFormatCtx->streams[idxVideoStream]->codec;


	//Find the decoder for the video stream
	AVCodec* pCodec = NULL;
	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if (pCodec == NULL)
		return -1; //unsupported codec

	//Copy Context
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0)
		return -1;// couldn't copy codec context

	//open codec
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
		return -1;

	avcodec_close(pCodecCtxOrig);
}

int CFFmpegPlayer::openAudioCodec()
{
	idxAudioStream = findStreamIndex(AVMEDIA_TYPE_AUDIO);
	if (idxAudioStream == -1)
		return -1;

	AVCodecContext* pCodecCtxOrig = NULL;
	pCodecCtxOrig = pFormatCtx->streams[idxAudioStream]->codec;

	AVCodec *pCodec;

	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if (pCodec == NULL)
		return -1;

	pAudioCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pAudioCodecCtx, pCodecCtxOrig) != 0)
		return -1;

	if(avcodec_open2(pAudioCodecCtx, pCodec, NULL) < 0)
		return -1;

	avcodec_close(pCodecCtxOrig);
}


AVCodecContext* CFFmpegPlayer::getAudioCodecContext()
{
	return  pAudioCodecCtx;
}

extern PacketQueue audioq;

int CFFmpegPlayer::play(void* drawFun, uint8_t *const dst[], const int dstStride[])
{
	//Allocate video Frame

	int frameFinished = 0;
	AVPacket packet;
	AVFrame *pFrame = av_frame_alloc();

	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		// Is this a packet from the video stream?
		if (packet.stream_index == idxVideoStream)
		{
			//Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			//Did we get a video
			if (frameFinished)
			{
				// convert the image into YUV Format that SDL uses
				static int sws_flags = SWS_BICUBIC;
				struct SwsContext *img_convert_ctx;
				img_convert_ctx = sws_getContext(
					pCodecCtx->width, pCodecCtx->height,
					pCodecCtx->pix_fmt,
					pCodecCtx->width, pCodecCtx->height,
					PIX_FMT_YUV420P,
					sws_flags, NULL, NULL, NULL);

				sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, dst, dstStride);//pict.data, pict.linesize);
				sws_freeContext(img_convert_ctx);


				pDrawFun draw = (pDrawFun)drawFun;

				draw();

			}
		}
		else if (packet.stream_index == idxAudioStream)
			packetQueuePut(&audioq, &packet);
		else
			av_free_packet(&packet);
	}

	av_free(pFrame);


	return 1;
}


int CFFmpegPlayer::reOpen(const char*fileName)
{
	avformat_close_input(&pFormatCtx);

	return fileOpen(fileName);

}


int CFFmpegPlayer::convertYuvToRgb()
{
	//// Allocate an AVFrame structure
	//AVFrame* pFrameRGB = av_frame_alloc();
	//if (pFrameRGB == NULL)
	//	return -1;

	//int numBytes;
	//uint8_t* buffer = NULL;

	////Determine required buffer size and allocate buffer
	//numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

	//buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

	////assign appropriate parts of buffer to image planes in FrameRGB
	////Note the pFrameRGB is an AVFrame, but AVFrame is z superset
	//// of AVPicture
	//avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);


	////Reading the Data
	//////initialize SWS Context for software scaling
	//struct SwsContext *sws_ctx = NULL;
	//
	//sws_ctx = sws_getContext(pCodecCtx->width,
	//	pCodecCtx->height,
	//	pCodecCtx->pix_fmt,
	//	pCodecCtx->width,
	//	pCodecCtx->height,
	//	PIX_FMT_RGB24,
	//	SWS_BILINEAR,
	//	NULL,
	//	NULL,
	//	NULL
	//	);

	//////Convert the image its native format to RGB

	//// 프레임에서 해줘야 함 윗 부분은 rgb로 변환하기 위한 init 작업
	////sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

	//av_free(buffer);
	//av_free(pFrameRGB);

	return -1;
}


int CFFmpegPlayer::findStreamIndex(AVMediaType streamType)
{
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == streamType)
			return i;
	}

	return -1;
}

void CFFmpegPlayer::rgbSaveFrameToFile(AVFrame *pFrame, int width, int height, int iFrame)
{
	FILE *pFile;
	char szFilename[32];
	int  y;

	// Open file
	sprintf_s(szFilename, "frame%d.ppm", iFrame);
	
	fopen_s(&pFile, szFilename, "wb");
	if (pFile == NULL)
		return;

	// Write header
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Write pixel data
	for (y = 0; y<height; y++)
		fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, width * 3, pFile);

	// Close file
	fclose(pFile);
}