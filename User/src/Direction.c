#include "Direction.h"
#include "ImgProc.h"
#include "fuzzy.h"
#include "PatternMatch.h"
#include "DataComm.h"

//int32_t dirGyro;
int16_t dirAngleSpeed;
//float dirAngleSpeedIntegral;
int32_t changeRate;
int16_t state;//传感器融合
int16_t preState;//传感器融合

int16_t DirectionErrorGet(int16_t* middleLine, int16_t expectMiddle);

#if defined(OUT_JUDGE) || defined(RS_JUDGE)
bool out;
#endif
#ifdef DYNAMIC_DC_PID
float DC_PID_P;
#endif

/**
 * @brief  方向PID准闭环, 有动态/静态P参数两种控制模式, 由DYNAMIC_DC_PID宏决定, 其于头文件Param.h中定义
 * @param[in]  leftSensor 左电感采集
 * \param[in]  middleSensor 中电感采集(可选)
 * \param[in]  rightSensor 右电感采集
 * \param[in]  dirAngleSpeed 方向陀螺仪采集(可选)
 * \param[in]  DC_PID_P 动态P, 经过计算得到(可选)
 * @retval 方向环输出, 作为标准电机输出的一环
 */
static int32_t Direction_PID(int32_t dirAngleSpeed
                             #ifdef DYNAMIC_DC_PID
                             , float DC_PID_P, int16_t DC_PID_D
                             #endif
) 
{
    int32_t P, D;
    int32_t incpid;
//    int16_t absState;

    state = DirectionErrorGet(resultSet.middleLine,IMG_COL / 2);
//    state = state * (state * state / 1250 + 2) / 10;
    if(state > 110)
			state = 110;
    if(state < -110)
        state = -110;
//    if(state >= 0)		
//		absState = state;
//	else
//        absState = -state;
    P = DC_PID_P * state / 25 ;

//        P = DC_PID_P * state * absState / 2500;    //  50 / 50
//        P =300 * state;                            //  4000/50
//    else 
//				P = DC_PID_P * state * absState / 3000 + absMiddleSlope * 200;    //  50 / 50 
		
    D = DC_PID_D * dirAngleSpeed / 100;
    
    incpid = P + D;
    
    return incpid;
}


int16_t DirectionErrorGet(int16_t* middleLine, int16_t expectMiddle) 
{
    int32_t avgMiddle = 0;
	for(int16_t i = MODE.pre_sight - 3; i < MODE.pre_sight + 3; i++) 
		avgMiddle += middleLine[i];

    avgMiddle /= 6;

    return avgMiddle - expectMiddle;
}


/**
 * @brief  标准方向环处理, 根据周期平滑输出, 周期在头文件Param.h中定义为DC_PERIOD
 * @param[in]  speed 由编码器采集的速度, 用于计算动态P
 * @retval 方向环输出, 作为标准电机输出的一环
 */
int32_t DirectionProc(int32_t speed)
{
    static uint8_t count = 0;
    static int16_t preDirAngleSpeed = 0;
    static int32_t DC_Out_Old = 0, DC_Out_New = 0;
    int32_t DC_Out;
    
    dirAngleSpeed = DirGyroGet();
    dirAngleSpeed = dirAngleSpeed * 0.9 + preDirAngleSpeed * 0.1;
    preDirAngleSpeed = dirAngleSpeed;
    if( count >= DC_PERIOD )
    {
        count = 0;
        DC_Out_Old = DC_Out_New;

                
        #ifdef DYNAMIC_DC_PID
            DC_PID_P = MODE.DC_PID_P_COEF * speed ;//* speed;
//            DC_PID_P = MODE.DC_PID_P_COEF * ((speed * speed) / (MODE.VC_Set * MODE.VC_Set) + 1);
//            printf("%f\n" , DC_PID_P);
            if( DC_PID_P > MODE.DC_P_MAX )
            {
                DC_PID_P = MODE.DC_P_MAX;
            }
            else if( DC_PID_P < MODE.DC_P_MIN )
            {
                DC_PID_P = MODE.DC_P_MIN;
            }
        #endif
                
        DC_Out_New = Direction_PID(dirAngleSpeed
                                   #ifdef DYNAMIC_DC_PID
                                   , DC_PID_P, MODE.DC_PID_D
                                   #endif
                                   );
            
        if( DC_Out_New > MODE.DC_Out_MAX )
        {
            DC_Out_New = MODE.DC_Out_MAX;
        }
        else if( DC_Out_New < -MODE.DC_Out_MAX )
        {
            DC_Out_New = -MODE.DC_Out_MAX;
        }
//		if(!ring_offset)
//		state = DirectionErrorGet(resultSet.middleLine,IMG_COL / 2);
//		if(state > 10)
//			state -= 1;
//		if(state < -10)
//			state += 1;

//        changeRate = (state - preState);
//        preState = state;
//        DC_Out_New = FUZZY_pid(state, changeRate);
    
    }
    count++;

    DC_Out = DC_Out_Old + (DC_Out_New - DC_Out_Old) * count / DC_PERIOD;
    
    return DC_Out;
}
