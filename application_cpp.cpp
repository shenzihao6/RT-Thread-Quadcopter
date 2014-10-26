#include "Led.h"
#include "stdio.h"
#include "rtthread.h"
#include "stm32f4xx.h"
#include "Quadx.h"
#include "Communication.h"
#include "cpu_usage.h"
#include "MPU6050.h"
#include "HMC5883L.h"
#include "MS5611.h"
#include "Receiver.h"
#include "Motor.h"
#include "I2Cdev.h"

struct param_t
{
	struct pid_t PID[4];
	int16_t accXOffset,accYOffset,accZOffset;
	int16_t gyroXOffset,gyroYOffset,gyroZOffset;
	int16_t magXOffset,magYOffset,magZOffset;
};

struct ctrl_t
{
	bool att,thro,coor;
	bool alt,trace,quadx;
};
struct ctrl_t ctrl = {0};

extern "C" 
{
	extern int16_t targetX,targetY;
	void rt_thread_entry_trace(void* parameter);
	uint8_t ov_7725_init(void);
}

extern Led led1,led2,led3;

void hardware_init(void);

void rt_thread_entry_main(void* parameter)
{
/*************************************
	declare variables
*************************************/	
	ctrl.quadx = ctrl.att = ctrl.thro = true; ctrl.alt = ctrl.trace = ctrl.coor = false;
	uint8_t rxData[RX_DATA_SIZE] = {0},txData[TX_DATA_SIZE];
	uint8_t major,minor;

/*************************************
	hardware init
*************************************/
	hardware_init();
	
/*************************************
	create thread
*************************************/
	/*led_thread*/
	rt_thread_t led_thread = rt_thread_create("led",
												rt_thread_entry_led,
												RT_NULL,
												192,//max used = 140 
												1,
												10);
	/*communication_thread*/
	rt_thread_t communication_thread = rt_thread_create("communication",
												rt_thread_entry_communication,
												RT_NULL,
												512,
												9,
												10);
	/*quadx_get_thread*/
	rt_thread_t quadx_get_thread = rt_thread_create("quadx_get_attitude",
												rt_thread_entry_quadx_get_attitude,
												RT_NULL,
												1024,
												7,
												10);
	/*quadx_control_thread*/
	rt_thread_t quadx_control_thread = rt_thread_create("quadx_control_attitude",
												rt_thread_entry_quadx_control_attitude,
												RT_NULL,
												1024,
												7,
												10);
												
	/*trace_thread*/
	rt_thread_t trace_thread = rt_thread_create("trace",
												rt_thread_entry_trace,
												RT_NULL,
												1024,
												10,
												500);

/*************************************
	start thread
*************************************/			
	if(led_thread != RT_NULL) rt_thread_startup(led_thread);
	if(communication_thread != RT_NULL) rt_thread_startup(communication_thread);
	if(quadx_get_thread != RT_NULL) rt_thread_startup(quadx_get_thread);
	if(quadx_control_thread != RT_NULL) rt_thread_startup(quadx_control_thread);
	if(trace_thread != RT_NULL) rt_thread_startup(trace_thread); 
	
	
	//让出cpu，队尾等待调度
	rt_thread_delay(50);
/*************************************
	main loop
*************************************/
	while(1)
	{
		//recv
		if(rt_mq_recv(rxQ,rxData,RX_DATA_SIZE,0) == RT_EOK)
		{
			if(rxData[0]>=0xda&&rxData[0]<=0xdd)
			{
				PID[rxData[0] - 0xda].P = rxData[1] / 10.0f;//P[0,20],精度0.1
				PID[rxData[0] - 0xda].I = rxData[2] / 1000.0f;//I[0,0.250],精度0.001
				PID[rxData[0] - 0xda].D = rxData[3] / 10.0f;//D[0,20],精度0.1
			}
			else if(rxData[0]==0xca)
			{
				//TODO: restart
				NVIC_SystemReset();
			}
			else if(rxData[0]==0xcb)
			{
				//四轴模式
				if(rxData[1] == 0xf1)
				{
					ctrl.quadx = true;
					led3.interval = 500;
				}
				else if(rxData[1] == 0xf0)
				{
					ctrl.quadx = false;
					led3.interval = 1000;
				}
			}
			else if(rxData[0]==0xcc)
			{
				//图像跟踪模式
				if(rxData[1] == 0xf1)
				{
					ctrl.trace = true;
					led1.interval = 500;
				}
				else if(rxData[1] == 0xf0)
				{
					ctrl.trace = false;
					led1.interval = 0xff;
				}
			}
			else if(rxData[0]==0xcd)
			{
				//发送的数据
				if(rxData[1] == 0xf1) ctrl.att = true;
				else if(rxData[1] == 0xf0) ctrl.att = false;
				if(rxData[2] == 0xf1) ctrl.thro = true;
				else if(rxData[2] == 0xf0) ctrl.thro = false;
				if(rxData[3] == 0xf1) ctrl.coor = true;
				else if(rxData[3] == 0xf0) ctrl.coor = false;
			}
			else if(rxData[0]==0xcf)
			{
				//校正
				if(!ctrl.quadx)
				{
					led3.interval = 100;
					//加计
					if(rxData[1] == 0xf1)
					{
						MPU6050 *accelgyro = new MPU6050();
						accelgyro->setOffset();
						delete accelgyro;
					}
					//罗盘
					else if(rxData[1] == 0xf0)
					{
						HMC5883L *mag = new HMC5883L();
						mag->setOffset();
						delete mag;
					}
					led3.interval = 1000;
				}
			}
			else
			{
				rt_kprintf("Unknown command!\r\n");
			}
		}
		//send
		if(ctrl.att)
		{
			uint8_t i;
			txData[0] = 0xea;
			for(i=0;i<3;i++)
				((int16_t*)(txData+1))[i] = att[i] * 10;//角度乘10，有符号
			((uint16_t*)(txData+1))[i] = att[i] * 50;//米乘50，无符号
			rt_mq_send(txQ,txData,TX_DATA_SIZE);
		}
		if(ctrl.thro)
		{
			uint8_t i;
			txData[0] = 0xeb;
			for(i=0;i<4;i++)
				((uint16_t*)(txData+1))[i] = motorValue[i];//电机不乘，无符号
			rt_mq_send(txQ,txData,TX_DATA_SIZE);
		}
		if(ctrl.coor)
		{
			txData[0] = 0xec;
			((int16_t*)(txData+1))[0] = targetX;//目标位置，不做变换
			((int16_t*)(txData+1))[1] = targetY;
			rt_mq_send(txQ,txData,TX_DATA_SIZE);
		}
		
//		char str[100];
//		sprintf(str,"%+f\t%+f\t%+f\t%+f\r\n",att[PITCH],att[ROLL],att[YAW],att[THROTTLE]);
//		sprintf(str,"%+d\t%+d\t%+d\t%+d\r\n",motorValue[0],motorValue[1],motorValue[2],motorValue[3]);
//		cpu_usage_get(&major,&minor);
//		sprintf(str,"major: %d\tminor: %d\r\n",major,minor);
//		rt_kprintf("%s",str);
		rt_thread_delay(25);
	}
}

void hardware_init(void)
{
	led1.initialize();
	led2.initialize();
	led3.initialize();
	led1.interval = 0xff;
	led2.interval = 0xff;
	led1.on();
	led2.on();
	led3.on();
	rt_thread_delay(1000);
	led1.off();
	led2.off();
		
	Receiver::initialize();
	I2Cdev::initialize();
	Motor::initialize();
	
	MPU6050 *accgyro = new MPU6050();
	while(!accgyro->initialize())
	{
		led2.toggle();
		rt_thread_delay(50);
	}
	delete accgyro;
	
	HMC5883L *mag = new HMC5883L();
	while(!mag->initialize())
	{
		led2.toggle();
		rt_thread_delay(200);
	}
	delete mag;
	
	MS5611 *baro = new MS5611();
	while(!baro->initialize())
	{
		led2.toggle();
		rt_thread_delay(350);
	}
	delete baro;
	
	if(!ov_7725_init())
		led1.interval = 100;
}

void param_init(void)
{
	
}

void param_save(void)
{
	
}

int  rt_application_init(void)
{	
	cpu_usage_init();
	rt_thread_t main_thread;
	main_thread = rt_thread_create("main",
									rt_thread_entry_main,
									RT_NULL,
									512,
									8,
									10);
	if(main_thread != RT_NULL)
		rt_thread_startup(main_thread);
	return 0;
}
