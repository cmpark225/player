extern "C"
{
#include "libavcodec\avcodec.h"
#include "libavformat\avformat.h"
#include "libswscale\swscale.h"
};


typedef void(*pDrawFun) ();


struct PacketQueue;

int packetQueuePut(PacketQueue *q, AVPacket *pkt);
static int packetQueueGet(PacketQueue *q, AVPacket *pkt, int block);

class CFFmpegPlayer
{
public:
	CFFmpegPlayer();

	~CFFmpegPlayer();

	int fileOpen(const char *fileName);

	int play(void* drawFun, uint8_t *const dst[], const int dstStride[]);

	int openCodec();
	
	int reOpen(const char*fileName);

	int getWidth() { if (pCodecCtx != NULL) return pCodecCtx->width; }
	int getHeight() { if (pCodecCtx != NULL) return pCodecCtx->height; }

	AVCodecContext* getAudioCodecContext();

private:

	int convertYuvToRgb();

	int findStreamIndex(AVMediaType streamType);

	void rgbSaveFrameToFile(AVFrame *pFrame, int width, int height, int iFrame);

	int openVideoCodec();
	int openAudioCodec();

private:

	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtx; 
	AVCodecContext *pAudioCodecCtx;


	int idxVideoStream;
	int idxAudioStream;

};