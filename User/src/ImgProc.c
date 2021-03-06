#include "ImgProc.h"
#include "gpio.h"
#include "DirectionControl.h"
#include "SpeedControl.h"
#include "ImgUtility.h"
#include "DataComm.h"
#include "uart.h"
#include "Velocity.h"
#include "Angle.h"

extern mode MODE;

#ifdef USE_BMP
byte imgBuf[IMG_ROW][1 + IMG_COL / 8]; // Important extra 1 byte against overflow
#else
byte imgBuf[IMG_ROW][IMG_COL];
#endif

extern bool start_deal;
bool onRamp;
int32_t rampDistance;

int16_t dirError;
bool direction_control_on;
img_proc_struct resultSet;

static uint8_t imgBufRow = 0;
static uint8_t imgRealRow = 0;
static int16_t searchForBordersStartIndex = IMG_COL / 2;

static int16_t GetPresight(int32_t speed, int16_t VC_Set, float offset);
static void ImgProcHREF(uint32_t pinxArray);
static void ImgProcVSYN(uint32_t pinxArray);
static void ImgProc0(void);
static void ImgProc1(void);
static void ImgProc2(void);
static void ImgProc3(void);
static void ImgProcSummary(void);

static img_proc_type_array imgProc = { ImgProc0, ImgProc1, ImgProc2 ,ImgProc3 };

#ifdef USE_BMP
inline void SetImgBufAsBitMap(int16_t row, int16_t col) 
{
    imgBuf[row][col >> SHIFT] |= (1 << (col & MASK));
}

inline void ClrImgBufAsBitMap(int16_t row, int16_t col) 
{
    imgBuf[row][col >> SHIFT] &= ~(1 << (col & MASK));
}

inline bool TstImgBufAsBitMap(int16_t row, int16_t col) 
{
    return imgBuf[row][col >> SHIFT] & (1 << (col & MASK));
}
#endif

void ImgProcInit(void) 
{
    GPIO_QuickInit(CAMERA_HREF_PORT, CAMERA_HREF_PIN, kGPIO_Mode_IPU);
    GPIO_QuickInit(CAMERA_VSYN_PORT, CAMERA_VSYN_PIN, kGPIO_Mode_IPU);
    GPIO_CallbackInstall(CAMERA_HREF_PORT, ImgProcHREF);
    GPIO_CallbackInstall(CAMERA_VSYN_PORT, ImgProcVSYN);
    GPIO_ITDMAConfig(CAMERA_HREF_PORT, CAMERA_HREF_PIN, kGPIO_IT_RisingEdge, DISABLE);
    GPIO_ITDMAConfig(CAMERA_VSYN_PORT, CAMERA_VSYN_PIN, kGPIO_IT_RisingEdge, DISABLE);
    
    GPIO_QuickInit(CAMERA_DATA_PORT, CAMERA_DATA_PIN, kGPIO_Mode_IPU);
	GPIO_QuickInit(CAMERA_ODEV_PORT, CAMERA_ODEV_PIN, kGPIO_Mode_IPU);
}

void ImgProcHREF(uint32_t pinxArray) 
{
    if(imgBufRow < IMG_ROW && imgRealRow > IMG_ABDN_ROW)
    {
        imgProc[imgRealRow % IMG_ROW_INTV]();
    }
    imgRealRow++;
}

void ImgProcVSYN(uint32_t pinxArray) 
{
    ImgProcSummary();
    imgRealRow = 0;
    imgBufRow = 0;
    resultSet.leftBorderNotFoundCnt = 0;
    resultSet.rightBorderNotFoundCnt = 0;
    searchForBordersStartIndex = IMG_COL / 2;
}

void ImgProc0() 
{
    int16_t i;
    for(i = 0; i <= IMG_READ_DELAY; i++) { } //ignore points near the border
    #ifdef USE_BMP
        static byte tmpBuf[IMG_COL]; //cache
        for(i = IMG_COL - 1; i >= 0; i--)
        {
            tmpBuf[i] = CAMERA_DATA_READ;
            __ASM("nop");__ASM("nop");__ASM("nop");__ASM("nop");__ASM("nop");__ASM("nop");
            __ASM("nop");__ASM("nop");__ASM("nop");__ASM("nop");
        }
        for(i = IMG_COL - 1; i >= 0; i--) 
        {
            if(tmpBuf[i])
                SetImgBufAsBitMap(imgBufRow, i);
            else
                ClrImgBufAsBitMap(imgBufRow, i);
        }
    #else
        for(i = IMG_COL - 1; i >= 0; i--) 
        {
            imgBuf[imgBufRow][i] = CAMERA_DATA_READ;
        }
    #endif
}

void ImgProc1() 
{
    resultSet.foundLeftBorder[imgBufRow] = LeftBorderSearchFrom(imgBufRow, searchForBordersStartIndex);
    resultSet.foundRightBorder[imgBufRow] = RightBorderSearchFrom(imgBufRow, searchForBordersStartIndex);
}

void ImgProc2() 
{
    MiddleLineUpdate(imgBufRow);
    searchForBordersStartIndex = resultSet.middleLine[imgBufRow];
}

void ImgProc3()
{
//    CurveSlopeUpdate(imgBufRow);
    imgBufRow++;
}

void ImgProcSummary()
{
	static unsigned short cnt = 0;
	static unsigned short error = 0;
	int i;
	    resultSet.imgProcFlag = 0;
    #ifdef DynamicPreSight
	MODE.pre_sight = Pre_Sight_Set + GetPresight(speed, MODE.VC_Set, MODE.pre_sight_offset);
	if(MODE.pre_sight < 4)
		MODE.pre_sight = 4;
    #endif
//	BUZZLE_OFF;
        if(StraightLineJudge())
        {
            for(i=40;i<IMG_ROW;i++)
            {
                if(((resultSet.rightBorder[i] - resultSet.leftBorder[i]) - (resultSet.rightBorder[i+1] - resultSet.leftBorder[i+1])) > 30)
                {
                    if((resultSet.rightBorder[IMG_ROW-1] - resultSet.leftBorder[IMG_ROW-1]) < 30 && time > 3)
                    {
                        resultSet.imgProcFlag = RAMP;
                        break;
                    }
                }
            }

		if(i==IMG_ROW)
		{
			resultSet.imgProcFlag = STRAIGHT_ROAD;
            ring_offset = 0;
            ring_end_offset = 0;
            }
        }
        #ifdef STOP
        if(!start_deal)
        {
            if(OutOfRoadJudge())
            {
				cnt++;
				if(cnt >= 1)
				{
					cnt = 0;
                    while(1)
                    {
                        MOTOR_STOP;
                    }
				}
            }
            if(StartLineJudge(MODE.pre_sight))
            {
                    while(1)
                    {
                        MOTOR_STOP;
                    }
            }
        }
        #endif
       switch(GetRoadType())
       {
            case Ring:
                if(!MODE.ringDir)
                    RingCompensateGoRight();
                else
                    RingCompensateGoLeft();

				resultSet.imgProcFlag = CIRCLE;
                /*分段offset
                if(ringDistance < 800)
                {
                    if(ring_offset >= 12)
                            ring_offset = ring_offset;
                    else
                            ring_offset += 2;
                }
                else
                {
                    if(ring_offset >= 24)
                            ring_offset = ring_offset;
                    else
                            ring_offset += 4;
                }
                if(ringDistance < 800)
                {
                    
                    if(ring_offset >= 8)
                            ring_offset = ring_offset;
                    else
                            ring_offset += 1;
                }
                else if(ringDistance < 1400)
                {
                    
                    if(ring_offset >= 12)
                            ring_offset = ring_offset;
                    else
                            ring_offset += 2;
                }
                else
                {
                    if(ring_offset >= 20)
                            ring_offset = ring_offset;
                    else
                            ring_offset += 4;
                }   */
                break;
            case RingEnd:
				resultSet.imgProcFlag = RINGEND;
                if(!MODE.ringDir)
                    RingEndCompensateFromRight();
                else
                    RingEndCompensateFromLeft();
                /*分段offset
                if(ringDistance < 500)
                {
                    if(ring_end_offset >= 6)
                            ring_end_offset = ring_end_offset;
                    else
                            ring_end_offset += 1;
                }
                else if(ringDistance < 800)
                {
                    if(ring_end_offset >= 16)
                            ring_end_offset = ring_end_offset;
                    else
                            ring_end_offset += 2;
                }
                else
                {
                    if(ring_end_offset >= 28)
                            ring_end_offset = ring_end_offset;
                    else
                            ring_end_offset += 4;
                }
                if(ringDistance < 800)
                {
                    if(ring_end_offset >= 12)
                            ring_end_offset = ring_end_offset;
                    else
                            ring_end_offset += 3;
                }
                else
                {
                    if(ring_end_offset >= 32)
                            ring_end_offset = ring_end_offset;
                    else
                            ring_end_offset += 5;
                }   */
                break;
//            case LeftCurve:
//                BUZZLE_OFF;
//                LeftCurveCompensate();
//				  resultSet.imgProcFlag = LEFTCURVE;
//				  ring_offset = 0;
//                break;
//            case RightCurve:
//                BUZZLE_OFF;
//				  resultSet.imgProcFlag = RIGHTCURVE;
//                ring_offset = 0;
//                RightCurveCompensate();
//                break;
            case CrossRoad:
                resultSet.imgProcFlag = CROSS_ROAD;
//                CrossRoadCompensate();
                break;
            case LeftBarrier:
                resultSet.imgProcFlag = BARRIER;
                LeftBarrierCompensate() ;
                break;
            case RightBarrier:
                resultSet.imgProcFlag = BARRIER;
                RightBarrierCompensate() ;
                break;
            default:
				break;
        }
}
/*角度动态前瞻
int16_t GetPresight(int16_t angle, int8_t mid, int8_t offset) {
    return angle < mid-4*offset ? -4 : angle < mid-3*offset ? -3 : angle < mid-2*offset ? -2 : angle < mid-offset ? -1 : angle < mid ? 0 :
        angle < mid+offset ? 1 : angle < mid+2*offset ? 2 : angle < mid+2*offset ? 3 : 4;
}*/
/*速度动态前瞻*/
int16_t GetPresight(int32_t speed, int16_t VC_Set, float offset) {
    return speed < VC_Set-4*offset ? -3 : speed < VC_Set-3*offset ?  -2 : speed < VC_Set-2*offset ? -1 : 
           speed < VC_Set+3*offset ?  0 : speed < VC_Set+5.5*offset ? 1 : 2;
}
